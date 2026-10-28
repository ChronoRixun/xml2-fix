#include "online_menus.hpp"
#include "lc_fonts.hpp"
#include "gameplay_agreement.hpp"
#include "online_room.hpp"
#include "gns_transport.hpp"
#include "gameplay_agreement.hpp"
#include "join_in_progress.hpp"
#include "online_menus_rules.hpp"
#include "online_menus_guards.hpp"
#include "online_menus_hits_rules.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "mod_loader.hpp"
#include "script_functions.hpp"
#include "script_function_overflow.hpp"
#include "patch_transaction.hpp"
#include <array>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <ctime>
#include <map>
#include <cstdio>
#include <intrin.h>
#include <atomic>
#include <vector>
#include <deque>

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
std::uint64_t default_wall() { return std::uint64_t(std::time(nullptr)); }
std::uint64_t (*wall_source)()=&default_wall; // epoch seconds; only the Hub's "N hours ago" reads it
std::uint64_t wall_seconds() { return wall_source(); }
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
bool (*pad_probe)(int)=nullptr; // see screen_mask
int menu_player=0;               // the controller that opened the Hub (its Accept press)
std::optional<recent_game> recent;
bool recent_loaded=false;
std::uint64_t fixture_ticket=0;
std::string fixture_code;
bool (*lobby_fields)(lobby_fixture&)=nullptr;
bool lobby_live(void* menu);
bool native_route_request(const char* name,bool console=false);
bool console_routed=false; // set by console_open (openmenu) for exactly one nested request_impl
void __fastcall request(void* m,void*,const char* name,const char* args);
void native_history_trim(void* menu);

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
// Bounded Unicode payload for the lobby Copy and the Party Invite. SetClipboardData takes ownership on success; the owner
// is this process's own existing game window (never created, activated or focused).
bool clipboard_copy(const std::string& shown) {
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
    return copied;
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
bool broker_live();
bool remember_game(const std::string& value,const std::string& title) {
    recent_game next{value,display(title,48),std::uint64_t(std::time(nullptr))};
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
bool remember_fixture() { return remember_game(code.text,"Offline fixture"); }
host_left host_left_state;
bool removed_notice=false; // the host removed this guest (native Kick): lc_notice_removed at the next Hub tick
bool hub_panel_shown=false; // the Hub last drew the host-left panel (focus moves only on a change)
toast guest_toast;
roster_watch roster;
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
    // console_open (openmenu) has already routed this request with console provenance; never route it twice.
    const bool routed=console_routed;console_routed=false;
    if(ready && !routed && !native_route_request(name)) return;
    pending.clear();keyboard_input.clear();
    if(name_edit.owned && name && std::strcmp(name,"text_entry") && std::strcmp(name,"lc_online") && !abandon_name()) return;
    auto* source=current();
    const auto source_id=reinterpret_cast<std::uintptr_t>(source);
    // The main menu is the root: leaving it with itself still stacked (Hub Back -> main pushed lc_online) would let a
    // later native close (Options Back) pop to the Hub. Same invariant as the custom screens' tick trim.
    if(ready && which(source)==screen::main) native_history_trim(source);
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
            entry.start(source_id,now_ms());error_visible=false;menu_player=p;
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
    if(ready && line) for(int value=2;value<=int(intent::last);++value) {
        if(value==int(intent::invalid_key)) continue; // internal keyboard feedback, never a published command
        if(value>62 && !party_intent(value)) continue; // 68 and 70 are reserved

        char command[48]{};
        std::snprintf(command,sizeof(command),"runscript lcOnlineIntent(%d)",value);
        if(std::strcmp(command,line)) continue;
        auto* menu=current();
        if(live(menu)) {
            if(pending.action || pending.post(value,reinterpret_cast<std::uintptr_t>(menu),which(menu))) return true;
            if(value>=18 && value<=31) return true; // wrong-scope broker intent is discarded, never delayed
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
// 0x5ade10 calls the found item's vt+8 setHidden. Text items inherit the base bit setter 0x5bcdb0;
// MENU_ITEM_MODEL (vtable 0x6a0304) overrides it with 0x5c4b90, which sets the same bit and swaps
// the model for ui/models/m_invis.igb. Live 060p: accepting only 0x5bcdb0 left every authored model
// scene (Host visibility pair, campaign / drop-in variants, lobby cards, badges, Leave panel,
// error scenes) at its XMLB start state, so PRIVATE stayed lit while the room posted public.
void hide_refused(const char* item,std::uint32_t vtable,std::uint32_t setter) {
    logger::write_once(std::string("online menus hide refused ")+item,"online menus: visibility of '%s' not set (item vtable %#x, setter %#x)",item,vtable,setter);
}
bool hide_native(void* menu,const char* item,bool hidden) {
    std::uint32_t setter=0,vtable=0;bool found_item=false;
    __try {
        auto* found=at<void*(__thiscall*)(void*,const char*)>(0x5adc10)(menu,item);
        found_item=found!=nullptr;
        vtable=found ? *static_cast<const std::uint32_t*>(found) : 0u;
        setter=found ? (*static_cast<const std::uint32_t**>(found))[8/4] : 0u;
        if(found && (setter==0x5bcdb0 || setter==0x5c4b90)) {
            at<hide_fn>(0x5ade10)(menu,item,hidden);return true;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { }
    if(found_item) hide_refused(item,vtable,setter);
    return false;
}
// ---- Model scenes authored hidden (0.6 preview fix G; 060q live failures 10, 5, 6) ---------------------------------------
// MENU_ITEM_MODEL's load (vt+0x44 = 0x5c5080) runs the base parse first (0x5bc7a0: hide="true" sets the hidden bit through
// setHidden 0x5c4b90 while the item has no model yet), then sets the authored model (vt+0x38 0x5bbdd0 -> 0x5c4c50). On a
// hidden item 0x5c4c50 draws ui/models/m_invis and RELEASES the requested model (0x575980) instead of keeping it in the
// saved slot +0x94 that a later setHidden(false) restores - so an item authored hidden can never be shown: the shown call
// succeeds and draws nothing (live 060q/G: Browse STATUS / PING / BUILD and the row selection, the Host PUBLIC label and
// campaign / drop-in variants, the lobby card frames and badges). For the Legends Classic online-menu models only (model
// path ui/models/lc_o_*; every retail model loads unchanged) the adapter finishes the load the way the engine supports:
// the authored model set while shown, then hidden through setHidden, which moves it to +0x94 for the next show.
unsigned kept_hidden_models=0;
void keep_hidden_model(void* it,void* node) {
    __try {
        auto* bytes=static_cast<unsigned char*>(it);
        if(*static_cast<const std::uint32_t*>(it)!=0x6a0304 || !(bytes[0x54]&4) || *reinterpret_cast<void* const*>(bytes+0x94)) return;
        const char* name=at<const char*(__thiscall*)(void*,const char*)>(0x5649c0)(node,reinterpret_cast<const char*>(0x6853fc)); // "model"
        if(!name || std::strncmp(name,"ui/models/lc_o_",15)) return;
        bytes[0x54]&=~4u;                                                                  // shown: 0x5c4c50 keeps the model
        at<void(__thiscall*)(void*,const char*,void*)>(0x5bbdd0)(it,name,nullptr);          // vt+0x38: the authored model
        at<void(__thiscall*)(void*,bool)>(0x5c4b90)(it,true);                               // hidden: model -> +0x94, m_invis drawn
        ++kept_hidden_models;
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
void __fastcall model_item_load(void* it,void*,void* node) {
    at<void(__thiscall*)(void*,void*)>(0x5c5080)(it,node);
    keep_hidden_model(it,node);
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
namespace broker = online_browser_core;
bool offline_fixture = false;
#if XML2_FIX_GNS
void production_broker_tick() { online_room::ui_tick(); gns_transport::room_ui_tick(); }
broker_api broker_calls{online_room::available,online_room::view,production_broker_tick,online_room::list,
    online_room::host,online_room::resolve,online_room::retry,online_room::cancel,online_room::leave,online_room::version,online_room::take_preset,online_room::refresh_due,online_room::host_native,online_room::admission_target,online_room::host_posted,join_in_progress::installed,online_room::leave_after_flush,online_room::party,online_room::room_rtt,gns_transport::remove_member,online_room::retry_in_ms,gameplay_agreement::refusal_rules,online_room::host_lost,gameplay_agreement::refusal_serial,online_room::removed_by_host};
#else
broker_api broker_calls{};
#endif
bool broker_live() { return broker_calls.available && broker_calls.available() && broker_calls.view; }
const broker::client_view& broker_view() {
    static const broker::client_view empty;
    return broker_live() ? broker_calls.view() : empty;
}
screen broker_screen = screen::none;
unsigned browse_offset = 0, browse_selected = 0;
std::string browse_code, host_room_name;
// Every per-cause error scene the DLL may select is lc_broker_err_<error_cause_key[i]> (LC authors the localized copy per
// screen; absent nodes are skipped).
// A cause the assets do not author (broker/2-only LC) falls back to the nearest scene that is true of it.
int fallback_cause(int i) {
    switch(error_cause(i)) {
    case error_cause::pin_mismatch: return int(error_cause::refused);
    case error_cause::code_expired: return int(error_cause::code_not_found);
    case error_cause::room_closed: case error_cause::test_mode: case error_cause::unsupported:
    case error_cause::busy: case error_cause::invalid_name: return int(error_cause::generic);
    default: return -1;
    }
}
void hide_error_scenes(void* menu,int shown=-1) {
    bool authored=true;
    for(unsigned i=0;i<std::size(error_cause_key);++i) {
        const bool usable=hide(menu,(std::string("lc_broker_err_")+error_cause_key[i]).c_str(),int(i)!=shown);
        if(int(i)==shown) authored=usable;
    }
    const int fb=shown>=0 && !authored ? fallback_cause(shown) : -1;
    if(fb>=0) hide(menu,(std::string("lc_broker_err_")+error_cause_key[fb]).c_str(),false);
}
std::uint32_t retry_wait_ms() { return broker_calls.retry_in ? broker_calls.retry_in() : 0; }
bool host_public = false;
std::string browse_join_code; // a Browse join waiting for the broker client (browse_join_tick)
std::uint64_t resolve_edit = 0;
broker::ticket completed_list_ticket{};
DWORD last_list_at=0,list_done_at=0;
bool listed_started=false,list_done=false;
broker::ticket resolve_ticket{}, remembered_ticket{};
void native_flow_cancel();
void broker_cancel(bool leave_room=false) {
    native_flow_cancel();
    if(!broker_live()) return;
    if(leave_room && broker_calls.leave) broker_calls.leave();
    else if(broker_calls.cancel) broker_calls.cancel();
    resolve_edit=0;resolve_ticket={};
}
#include "online_native_flow.inl"
void broker_scope(screen s) {
    if(!broker_live()) return;
    if(broker_screen!=s) {
        if(broker_screen==screen::browse || broker_screen==screen::code || broker_screen==screen::host) {
            const auto state=broker_view().state;
            if(state!=broker::phase::room) broker_cancel();
        }
        broker_screen=s;
        if(s==screen::host && host_room_name.empty()) host_room_name=online_browser_core::room_name(std::string(local_name)+"'s game");
    }
}
void broker_pump(screen s) {
    if(!broker_live()) return;
    if(broker_calls.tick) broker_calls.tick();
    const auto& state=broker_view();
    if(state.list_known && state.state==broker::phase::idle && state.action==broker::operation::list && state.generation!=completed_list_ticket) {
        completed_list_ticket=state.generation;last_list_at=now_ms();listed_started=true;
        list_done_at=last_list_at;list_done=true; // the list's age counts from its completion, not its request
    }
    if(s==screen::browse && browse_join_code.empty() && state.state!=broker::phase::refused && state.state!=broker::phase::retry_wait &&
        state.state!=broker::phase::room && broker_calls.list &&
        (state.state==broker::phase::idle) && broker_calls.view &&
        // 10-second idle refresh, including an empty successful list.
        (broker_calls.refresh_due ? broker_calls.refresh_due() : !listed_started || now_ms()-last_list_at>=broker::list_refresh_ms)) {
        if(broker_calls.list()) { last_list_at=now_ms();listed_started=true; }
    }
}
// ---- Browse Join (0.6 preview fix G; 060q live failure 2) -------------------------------------------------------------
// Live 060q: Enter / pad A / a click on a listed row and the Join prompt started nothing. Two causes: the row's command
// (24+R) only selected the row, and the Join prompt's single resolve call was refused without a word whenever the shared
// 2-second lookup limiter was still closed or a list request was in flight - which the 10-second auto refresh and every
// F5 make the normal case on Browse (the fixtures advanced their clock 2 s before every Join, so they passed). Now focus
// moving over the rows moves the selection, the selected row's command joins it, the prompt joins the selected one, and
// the join is held (no new list starts meanwhile) until the broker
// client takes it: an in-flight list is cancelled, the limiter's wait (<= 2 s) is waited out, then the code resolves and
// joins exactly like Join by code. Every outcome is logged once.
void* item(void* menu,const char* name);
DWORD browse_join_since=0;const char* browse_join_via="";
int focused_browse_row(void* menu) {
    __try {
        auto* focused=*reinterpret_cast<void**>(static_cast<char*>(menu)+0x324); // CMenu's focused item (setFocus writes it)
        if(!focused) return -1;
        static const char* const cells[]{"0","1","2","3","6"};
        char key[16];
        for(unsigned r=0;r<browse_rows;++r) for(const char* c:cells) {
            std::snprintf(key,sizeof(key),"room_%u_%s",r,c);
            if(item(menu,key)==focused) return int(r);
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return -1;
}
const char* browse_join_refusal(const broker::client_view& v,const broker::public_room* row,const std::string& version) {
    if(!row) return "no listed room is selected";
    if(v.stale) return "the list is out of date - Refresh first";
    if(!row->joinable) return "the room does not take players";
    if(row->password) return "the room has a password";
    if(row->players>=row->max_players) return "the room is full";
    if(!row->version.empty() && row->version!=version) return "the room runs another build";
    if(!broker_calls.resolve) return "no online service client";
    return nullptr;
}
void browse_join(const char* via) {
    if(!broker_live()) { logger::write("online menus: Browse join (%s): the online service is not available",via);return; }
    const auto& v=broker_view();const auto version=broker_calls.version ? broker_calls.version() : std::string();
    const auto* row=browse_selected<v.rows.size() ? &v.rows[browse_selected] : nullptr;
    if(const auto* why=browse_join_refusal(v,row,version)) { logger::write("online menus: Browse join (%s) not started: %s",via,why);return; }
    browse_join_code=row->code;browse_join_since=now_ms();browse_join_via=via;
}
// Each Browse tick, before the auto refresh: hand a held join to the broker client.
void browse_join_tick(screen s) {
    if(browse_join_code.empty()) return;
    if(s!=screen::browse || !broker_live()) { browse_join_code.clear();return; }
    const auto& v=broker_view();
    const bool refreshing=v.action==broker::operation::list && (v.state==broker::phase::listing || v.state==broker::phase::authenticating);
    if(refreshing && broker_calls.cancel) broker_calls.cancel(); // the join wins over a refresh in flight
    if(broker_calls.resolve && broker_calls.resolve(browse_join_code)) {
        logger::write("online menus: Browse join (%s): room lookup started after %u ms; the native join follows as for Join by code",
                      browse_join_via,unsigned(now_ms()-browse_join_since));
        browse_join_code.clear();return;
    }
    if(now_ms()-browse_join_since>5000) {
        logger::write("online menus: Browse join (%s) not started: the online service client did not take the lookup within 5 s (state %d)",
                      browse_join_via,int(v.state));
        browse_join_code.clear();
    }
}
void bind_broker(void* menu,screen s) {
    if(!broker_live()) {
        if(!offline_fixture) {
            bind_text(menu,"service","Unavailable");bind_text(menu,"self_state","Unavailable");
            // No service: nothing connected, no error scene for a service that was never asked, and a host card whose
            // visibility pair is consistent from the first frame (private is the default).
            hide(menu,"lc_service_ok",true);
            hide_error_scenes(menu);bind_text(menu,"broker_error","");
            hide(menu,"retry",true);enable(menu,"retry",false);
            if(s==screen::host) { hide(menu,"lc_host_public",!host_public);hide(menu,"lc_host_private",host_public); }
        }
        return;
    }
    const auto& v=broker_view();
    const bool waiting=v.state==broker::phase::authenticating || v.state==broker::phase::listing || v.state==broker::phase::hosting || v.state==broker::phase::resolving;
    const bool failed=v.state==broker::phase::retry_wait || v.state==broker::phase::error || v.state==broker::phase::refused;
    const char* service=v.authenticated ? "Connected" : waiting ? "Connecting" : failed ? "Unavailable" : "Not connected";
    // A plainly connected service is drawn by the lc_service_ok scene (green diamond + localized word); its text fields
    // stay empty then so the word is never drawn twice. Every other state keeps the neutral text.
    const bool connected_plain=v.authenticated && !waiting && !failed && v.state!=broker::phase::room;
    hide(menu,"lc_service_ok",!connected_plain);
    bind_text(menu,"service",connected_plain ? "" : service);
    bind_text(menu,"self_state",connected_plain ? "" : waiting ? "Please wait" : v.state==broker::phase::room ? "Room connected" : service);
    const auto browse_hint=v.list_known ? std::to_string(v.rows.size()).append(v.stale ? " last known games" : " public games") : std::string("Browse public games");
    bind_text(menu,"browse_hint",browse_hint.c_str());
    bind_text(menu,"browse_hint_on",browse_hint.c_str()); // the focused row's navy twin (hub_focus_follow)
    // LC localizes these mutually exclusive variants at the approved status/error anchors.
    const bool native_tunnel=native_flow.phase==native_phase::tunnel,native_discovery=native_flow.phase==native_phase::discovery,
        native_admission=native_flow.phase==native_phase::admitting,native_posting=native_flow.phase==native_phase::host && !native_flow.lobby_active;
    // The room line and a native step share one status anchor in LC: while a native step is shown the room line is not.
    const bool native_step=native_tunnel || native_discovery || native_admission || native_posting;
    const std::pair<const char*,bool> variants[]{
        {"lc_native_tunnel",native_tunnel},{"lc_native_discovery",native_discovery},
        {"lc_native_admission",native_admission},{"lc_native_posting",native_posting},
        {"lc_broker_auth",v.state==broker::phase::authenticating},{"lc_broker_list",v.state==broker::phase::listing},
        {"lc_broker_host",v.state==broker::phase::hosting},{"lc_broker_resolve",v.state==broker::phase::resolving},
        {"lc_broker_empty",s==screen::browse && v.list_known && !waiting && !failed && v.rows.empty()},
        {"lc_broker_stale",v.stale && v.state!=broker::phase::room},{"lc_broker_room",v.state==broker::phase::room && !native_step},
        {"lc_broker_cancel",v.state==broker::phase::canceling || v.state==broker::phase::leaving}};
    for(const auto& variant:variants) hide(menu,variant.first,!variant.second);
    // Exactly one cause scene while failed. The raw id stays a bounded diagnostic and is never a command.
    const auto cause=classify_error(v.error);
    hide_error_scenes(menu,failed ? int(cause) : -1);
    // Retry: hidden where it cannot help and in the client's refused phase (online_room::retry() only accepts retry_wait/error;
    // the screen's own Done/Open/Refresh starts the next attempt there). Visible but disabled during the shared limiter/backoff,
    // and the diagnostic field counts down meanwhile - only where Retry is allowed (Refresh cannot bypass the limiter either).
    const bool retry_ok=(v.state==broker::phase::retry_wait || v.state==broker::phase::error) && cause_retryable(cause);
    const auto wait=failed && retry_ok ? retry_wait_ms() : 0;
    // Redacted diagnostic: the operation and a known id only (never broker text, codes or addresses). Native notices name
    // their step in the copy itself and log the fixed internal reason.
    // Another build: the diagnostic field names both tokens instead (<= 8 allowlisted characters each; the host's token only
    // exists for this failure).
    std::string diag=!failed ? std::string() : wait ? "Retry in "+std::to_string(wait_seconds(wait))+" s" : diagnostic(operation_name(int(v.action)),v.error);
    if(failed && cause==error_cause::version_mismatch && !v.error_detail.empty()) {
        const auto own=broker_calls.version ? broker_calls.version() : std::string();
        if(!own.empty()) diag=display(v.error_detail,8)+" vs your "+display(own,8);
    }
    bind_text(menu,"broker_error",diag.c_str());
    hide(menu,"lc_broker_offline_room",v.state!=broker::phase::room || v.authenticated);
    // Retry is hidden, not merely disabled, where trying again cannot help (wrong code, full room, other build,
    // refused service); the client's refused phase never retries either.
    hide(menu,"retry",!retry_ok);
    enable(menu,"retry",retry_ok && wait==0);
    if(s==screen::code) {
        const bool status=waiting || failed || v.state==broker::phase::room || error_visible;
        hide(menu,"code_status",status || code.text.size()==8);
        hide(menu,"lc_code_complete",status || code.text.size()!=8);
        enable(menu,"done",code.text.size()==8 && !waiting && v.state!=broker::phase::room);
    }
    if(s==screen::host) {
        const bool settings=native_flow.maximum!=0;
        // Drop all round-1 fixture prose; LC composes localized labels from
        // these variants and the numeric maximum at the same approved anchors.
        for(const auto* field:{"campaign","save","preview_save","preview_slots","drop_in","preview_drop"}) bind_text(menu,field,"");
        // 060q live: an empty value read as missing. Unknown until native Game Options produced it: the honest "--".
        bind_text(menu,"lc_host_max_players",settings ? std::to_string(native_flow.maximum).c_str() : "--");
        const std::pair<const char*,bool> choices[]{
            {"lc_host_settings_unknown",!settings},{"lc_host_new",settings && !native_flow.loaded},
            {"lc_host_load",settings && native_flow.loaded},{"lc_host_campaign",settings && !native_flow.danger_room},
            {"lc_host_danger_room",settings && native_flow.danger_room},{"lc_host_easy",settings && native_flow.difficulty==0},
            {"lc_host_normal",settings && native_flow.difficulty==1},{"lc_host_hard",settings && native_flow.difficulty==2}};
        for(const auto& choice:choices) hide(menu,choice.first,!choice.second);
        const bool policy_known=broker_calls.drop_in!=nullptr;
        const bool drop_in=policy_known && broker_calls.drop_in();
        hide(menu,"lc_host_drop_unknown",policy_known);hide(menu,"lc_host_drop_on",!policy_known || !drop_in);
        hide(menu,"lc_host_drop_off",!policy_known || drop_in);
        bind_text(menu,"room_name",display(host_room_name,32).c_str());
        bind_text(menu,"preview_name",display(host_room_name,24).c_str());
        bind_text(menu,"room_code",v.code.empty() ? "---- ----" : v.code.c_str());
        hide(menu,"lc_host_public",!host_public);hide(menu,"lc_host_private",host_public);
        enable(menu,"open",!waiting && v.state!=broker::phase::room);
        enable(menu,"prompt_open",!waiting && v.state!=broker::phase::room);
    }
    if(s!=screen::browse) return;
    if(const int focused_row=focused_browse_row(menu);focused_row>=0 && browse_offset+unsigned(focused_row)<v.rows.size()) {
        browse_selected=browse_offset+unsigned(focused_row);browse_code=v.rows[browse_selected].code; // the selection follows focus
    }
    if(!browse_code.empty()) {
        auto found=std::find_if(v.rows.begin(),v.rows.end(),[](const broker::public_room& r) { return r.code==browse_code; });
        if(found!=v.rows.end()) browse_selected=unsigned(found-v.rows.begin()); else browse_code.clear();
    }
    if(browse_selected>=v.rows.size()) browse_selected=0;
    if(!v.rows.empty()) browse_code=v.rows[browse_selected].code;
    if(browse_offset>browse_selected) browse_offset=browse_selected;
    if(browse_selected>=browse_offset+browse_rows) browse_offset=browse_selected-(browse_rows-1);
    const auto version=broker_calls.version ? broker_calls.version() : std::string();
    // List header: count and age. The age is DLL-ticked from the completion of the last good list.
    bind_text(menu,"list_count",v.list_known ? (std::string("- ")+std::to_string(v.rows.size())).c_str() : "");
    const bool aged=v.list_known && list_done;
    const std::uint32_t age=aged ? std::uint32_t(now_ms()-list_done_at) : 0;
    bind_text(menu,"list_age",aged ? age_text(age).c_str() : "");
    for(int k=1;k<=5;++k) hide(menu,("lc_age_fill"+std::to_string(k)).c_str(),!(aged && age_segments(age)>=k));
    for(unsigned i=0;i<browse_rows;++i) {
        const auto index=browse_offset+i;
        const bool present=index<v.rows.size();
        const auto* row=present ? &v.rows[index] : nullptr;
        std::string fields[7];
        row_status status=row_status::lobby;build_relation build=build_relation::check;bool dim=false;int pips=0;
        if(row) {
            status=classify_row(row->players,row->max_players,row->joinable,row->in_progress);
            build=relate_build(row->version,version);
            pips=pip_count(row->players);
            dim=status==row_status::full || status==row_status::closed || build==build_relation::other || row->password;
            fields[0]=display(row->name.empty() ? "Unnamed game" : row->name,24);
            fields[1]=display(row->host_name.empty() ? "Unknown" : row->host_name,12);
            fields[2]=display(row->zone.empty() ? "--" : row->zone,17);
            fields[3]=(row->players<0 ? std::string("--") : std::to_string(row->players))+"/"+std::to_string(row->max_players);
            // Status word, ping ("--": the broker sends no measurement and no connection is opened per row) and the
            // SAME/CHECK words are drawn by the row scenes, so there are no cells [4]/[5]. [6]: only the other build's token.
            fields[6]=build==build_relation::other ? display(row->version,8) : std::string();
        }
        if(broker_calls.room_rtt) {
            // Round 7: column 5 shows a measured RTT only for the room this PC is in (its host tunnel); no
            // connection is opened per row, so every other row stays the neutral "--" scene.
            const int rtt=row ? broker_calls.room_rtt(row->code) : -1;
            if(rtt>=0 && rtt<=60000) fields[5]=std::to_string(rtt)+" ms";
            const auto key="room_"+std::to_string(i)+"_5";
            bind_text(menu,key.c_str(),fields[5].c_str());
        }
        for(const unsigned col:{0u,1u,2u,3u,6u}) {
            const auto key="room_"+std::to_string(i)+"_"+std::to_string(col);
            bind_text(menu,key.c_str(),fields[col].c_str());enable(menu,key.c_str(),present && !waiting);
        }
        const auto id=std::to_string(i);
        for(unsigned k=0;k<std::size(row_status_key);++k) hide(menu,("lc_row"+id+"_st_"+row_status_key[k]).c_str(),!(present && unsigned(status)==k));
        for(int k=0;k<=4;++k) hide(menu,("lc_row"+id+"_pips_"+std::to_string(k)).c_str(),!(present && pips==k));
        for(unsigned k=0;k<std::size(build_key);++k) hide(menu,("lc_row"+id+"_build_"+build_key[k]).c_str(),!(present && unsigned(build)==k));
        hide(menu,("lc_row"+id+"_dim").c_str(),!(present && dim));
        const auto key="lc_row"+id+"_selected";hide(menu,key.c_str(),!present || index!=browse_selected);
    }
    const auto* selected=v.rows.empty() ? nullptr : &v.rows[browse_selected];
    bind_text(menu,"details_name",selected ? display(selected->name,24).c_str() : "");
    {
        // One honest line about the selected row. The empty-list message belongs to the lc_broker_empty scene alone.
        std::string line;
        if(selected) {
            line="Friends join at the next area";
            switch(relate_build(selected->version,version)) {
            case build_relation::same: line+=" - Same build as you ("+display(version,8)+")";break;
            case build_relation::other: line+=" - Other build ("+display(selected->version,8)+" vs your "+display(version,8)+")";break;
            case build_relation::check: line+=" - Build not checked";break;
            }
        } else if(v.stale) line="Refresh to update games";
        bind_text(menu,"details",line.c_str());
    }
    const bool join=selected && !waiting && !v.stale && selected->joinable && !selected->password && selected->players<selected->max_players &&
        (selected->version.empty() || selected->version==version);
    enable(menu,"prompt_join",join);
    enable(menu,"prompt_refresh",!waiting && retry_wait_ms()==0);
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
    DWORD copied_at=0; // when "Code copied" was raised: it expires (copied_ms) so it can never sit unseen in front of Back
    void reset(void* menu=nullptr) { ++generation;owner=menu;copied_at=0;confirm=error=copied=copy_held=role_held=focus_confirm=leave_authorized=copy_keyboard=copy_block=target_error=picker_safe=start_dimmed=start_was_enabled=false; }
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
// Pad buttons on the SKINNED lobbies (0.6 preview fix E; the custom screens' scheme, online_menus_rules pad_menu_mask): the
// designs put Ready (guest) / Kick (host) on X and Copy on Y, the engine's pad bits put them on Y (MENU_OTHER) and RT
// (MENU_NEXT). While the lobby's player is shown the pad, the lobby tick and the native lobby tick it wraps read X as
// MENU_OTHER and Y / RT as MENU_NEXT, and the prompts draw $MENU_OTHER / $MENU_NEXT as X / Y (pad_prompt_scope). The keyboard,
// an unskinned native lobby (no lc_lobby_v1) and Prompts=off (no device tracker) read the raw bits as before.
bool lobby_pad_shown(void* menu);
unsigned lobby_screen_mask(void* menu,unsigned raw) { return lobby_pad_shown(menu) ? pad_menu_mask(raw) : raw; }
unsigned __fastcall lobby_input(void* mgr,void*) {
    unsigned mask=0;lobby_mask(mgr,mask);
    const auto caller=reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    if(lobby_ticking && input_generation==lobby_state.generation && current()==lobby_state.owner && (caller==0x5bad33 || caller==0x5c88d2)) {
        mask=lobby_screen_mask(lobby_state.owner,mask);
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
// The lobby's player: the controller the menu manager follows (0x5d8570; the lobby may have opened from Other ways to play,
// not from the Hub), else the Hub's.
int lobby_player() {
    int controller=-1;
    __try { controller=at<int(__thiscall*)(void*)>(0x5d8570)(manager()); }
    __except(EXCEPTION_EXECUTE_HANDLER) { controller=-1; }
    return controller_valid(controller) ? controller : menu_player;
}
bool lobby_pad_shown(void* menu) { return pad_probe && lobby_live(menu) && pad_probe(lobby_player()); }
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
    __try {
        // The host lobby's only native Leave: its tick (0x5bacd0) entered with phase +0x1960 == 2 (set by Back / LEAVE on
        // the previous tick) tears the session down (0x607c40(0)) and closes the menu. Latch it for the deactivation that
        // follows (native_lobby_departing); a Start Game deactivation never passes here with phase 2.
        if(original==0x5bacd0 && *reinterpret_cast<const int*>(static_cast<const char*>(menu)+0x1960)==2) native_flow.host_leave=true;
        at<tick_fn>(original)(menu);return true;
    }
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
void* focus_of(void* menu);
void* item_try(void* menu,const char* name);
// 060q live (sl1): after Copy / a service loss the bar sat on Copy code or KICK PLAYER while the Leave confirm's Leave / Cancel
// stayed drawn (Up / Down leave the confirm's two buttons - they share the lobby's focus ring), and after Esc the focus could rest
// on a hidden item (no bar drawn at all). Every lobby tick, after the visibility is bound: while the confirm is up the focus is
// one of its two buttons; while it is not - or the Dismiss notice is not showing - the focus is never on one of those items.
void lobby_focus_guard(void* menu) {
    void* const focused=focus_of(menu);
    if(!focused) return;
    void* const yes=item_try(menu,"lc_leave_yes");
    void* const cancel=item_try(menu,"lc_leave_cancel");
    void* const dismiss=item_try(menu,"lc_lobby_dismiss");
    const bool on_confirm=focused==yes || focused==cancel;
    const bool dismiss_shown=(lobby_state.error || lobby_state.copied) && !lobby_state.confirm;
    if(lobby_state.confirm) { if(!on_confirm && cancel) lobby_focus(menu,"lc_leave_cancel"); }
    else if(on_confirm || (focused==dismiss && !dismiss_shown)) restore_lobby_focus(menu);
}
const char* route_text(int route) { return route==1 ? "direct" : route==2 ? "relay" : ""; }
const char* phase_text(int phase) {
    static const char* const words[]={"Connecting","Checking build","In lobby","Ready","Waiting for next area","Receiving campaign","Loading","Playing","Watching story",""};
    return phase>=0 && phase<int(std::size(words)) ? words[phase] : "";
}
// Card facts: host fixtures (lobby_fields) or, in production, online_room::party
// (round 7). Unknown RTT/build/phase stay unknown (Measuring / Checking / empty).
void bind_lobby(void* menu,const lobby_view& view,bool valid) {
    lobby_fixture extra{};
    bool fixture=lobby_fields && lobby_fields(extra) && extra.count<=4 && extra.pending_count<=3;
    if(!fixture && broker_live() && broker_calls.party) {
        // Production card facts (round 7). Rejected whole when out of bounds: the cards fall back to native rows.
        extra={};
        fixture=broker_calls.party(extra) && extra.count<=4 && extra.pending_count<=3;
        if(!fixture) extra={};
    }
    if(!fixture && broker_live()) {
        const auto& state=broker_view();
        if(state.state==broker::phase::room && state.code.size()==8) {
            std::memcpy(extra.code,state.code.c_str(),9);fixture=true;
        }
    }
    if(broker_live() && broker_view().state==broker::phase::room) {
        const auto& state=broker_view();
        if(!state.room_name.empty()) bind_text(menu,"lc_room_name",display(state.room_name,32).c_str());
        if(state.room_metadata_known) bind_text(menu,"lc_room_visibility",state.public_room ? "Public" : "Private");
    }
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
        const bool copied=clipboard_copy(shown);
        lobby_state.copied=copied;lobby_state.error=!copied;lobby_state.copied_at=now_ms();
    }
    if(lobby_state.copied && now_ms()-lobby_state.copied_at>=lobby_copied_ms) lobby_state.copied=false;
    bind_text(menu,"lc_room_code",shown.c_str());
    enable(menu,"lc_copy_code",bool(code_value));
    for(unsigned i=0;i<4;++i) {
        const auto prefix="lc_p"+std::to_string(i+1)+"_";
        const bool member=valid && i<view.count;
        // A broker-roster member without a native row yet takes the next free card (never a native row/kick target).
        const unsigned pending_slot=valid && i>=view.count ? i-view.count : 99;
        const auto* waiting_member=fixture && pending_slot<extra.pending_count ? &extra.pending[pending_slot] : nullptr;
        const bool card_shown=member || waiting_member;
        hide(menu,(prefix+"card").c_str(),!card_shown);
        for(const auto* field:{"empty","empty_title","empty_help","empty_plus"}) hide(menu,(prefix+field).c_str(),card_shown);
        for(const auto* field:{"name","slot","hero_fallback"}) hide(menu,(prefix+field).c_str(),!card_shown);
        hide(menu,(prefix+"pending").c_str(),!waiting_member);
        if(waiting_member) {
            const auto* raw=waiting_member->name;
            const auto shown_name=raw[0] ? display(std::string(raw,strnlen(raw,31)),31) : std::string("Player");
            bind_text(menu,(prefix+"name").c_str(),shown_name.c_str());
            bind_text(menu,(prefix+"slot").c_str(),("P"+std::to_string(i+1)).c_str());
            for(const auto* field:{"ready","not_ready","host","you","selected"}) hide(menu,(prefix+field).c_str(),true);
            const bool measured=waiting_member->ping_ms>=0 && waiting_member->ping_ms<=60000;
            bind_text(menu,(prefix+"ping").c_str(),measured ? std::to_string(waiting_member->ping_ms).c_str() : "");
            hide(menu,(prefix+"ping").c_str(),!measured);hide(menu,(prefix+"ms").c_str(),!measured);hide(menu,(prefix+"measuring").c_str(),measured);
            hide(menu,(prefix+"to_host").c_str(),!(measured && waiting_member->ping_to_host));
            bind_text(menu,(prefix+"route").c_str(),measured ? route_text(waiting_member->route) : "");
            hide(menu,(prefix+"same_build").c_str(),!(waiting_member->build==2 || waiting_member->build==3));
            hide(menu,(prefix+"other_build").c_str(),waiting_member->build!=1);
            hide(menu,(prefix+"checking_build").c_str(),waiting_member->build!=0);
            bind_text(menu,(prefix+"phase").c_str(),phase_text(waiting_member->phase));
            hide(menu,(prefix+"hero_fallback").c_str(),false);
            continue;
        }
        if(!member) {
            for(const auto* field:{"ready","not_ready","host","you","ping","ms","measuring","same_build","checking_build","selected","to_host","other_build"})
                hide(menu,(prefix+field).c_str(),true);
            bind_text(menu,(prefix+"phase").c_str(),"");
            bind_text(menu,(prefix+"route").c_str(),"");
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
        const bool self_row=view.local_known && view.ids[i]==view.local;
        const bool measured=data && data->ping_ms>=0 && data->ping_ms<=60000 && !self_row;
        bind_text(menu,(prefix+"ping").c_str(),measured ? std::to_string(data->ping_ms).c_str() : "");
        hide(menu,(prefix+"ping").c_str(),!measured);
        hide(menu,(prefix+"ms").c_str(),!measured);
        hide(menu,(prefix+"measuring").c_str(),measured || self_row);
        hide(menu,(prefix+"to_host").c_str(),!(measured && data->ping_to_host));
        bind_text(menu,(prefix+"route").c_str(),measured ? route_text(data->route) : "");
        // Build: round-7 stage when supplied (0 checking, 1 other, 2 same, 3 agreed), else round 4's same_build.
        const int stage=!data ? 0 : data->build>=0 ? data->build : data->same_build ? 2 : 0;
        hide(menu,(prefix+"same_build").c_str(),!(stage==2 || stage==3));
        hide(menu,(prefix+"checking_build").c_str(),stage==2 || stage==3 || stage==1);
        hide(menu,(prefix+"other_build").c_str(),stage!=1);
        // Phase words from observed facts; native readiness keeps its own READY / NOT READY parts above.
        // Without party facts (the service went away, or the room layer has none) the member is still in this native lobby:
        // that is observed, so the card keeps "In lobby" instead of losing its phase (060q live: service loss emptied it).
        bind_text(menu,(prefix+"phase").c_str(),data ? phase_text(data->phase) : phase_text(2));
        const bool picked=native_valid && host && native.phase==1 && int(i)==view.selected;
        hide(menu,(prefix+"selected").c_str(),!picked);
    }
    // A guest who left (or was lost) before the game started: a six-second named toast; the party continues.
    if(valid) {
        const auto gone=roster.observe(view.ids,view.names,view.count,view.local,view.local_known);
        if(!gone.empty()) guest_toast.show(display(gone,31),now_ms());
    } else roster.clear();
    const bool toast_on=guest_toast.visible(now_ms());
    // 060q live: the inline messages (toast, Copy / Kick notice, Leave confirm) draw on the row the ready line owns, and the
    // departed guest's bare name sat over "1 of 1 ready.". While one shows, the ready line is not drawn (the message names
    // what matters; the line returns with the message).
    const bool row_busy=toast_on || lobby_state.error || lobby_state.copied || lobby_state.confirm;
    bind_text(menu,"lc_ready_count",row_busy ? "" : std::to_string(valid ? view.ready_count : 0).c_str());
    bind_text(menu,"lc_member_count",row_busy ? "" : std::to_string(valid ? view.count : 0).c_str());
    unsigned waiting=0;while(waiting<view.count && view.ready[waiting]) ++waiting;
    bind_text(menu,"lc_waiting_name",valid && !row_busy && waiting<view.count ? display(view.names[waiting],31).c_str() : "");
    hide(menu,"lc_ready_summary",row_busy);
    hide(menu,"lc_ready_waiting",row_busy || !valid || waiting==view.count);
    hide(menu,"lc_ready_all",row_busy || !valid || waiting!=view.count || view.count<2);
    hide(menu,"lc_ready_solo",row_busy || !valid || view.count!=1 || waiting!=view.count);
    hide(menu,"lc_leave_confirm",!lobby_state.confirm);
    {   // The inline lobby messages never close Host/Join. The error names its cause (Kick target or Copy) when LC authors that scene.
        const bool err=lobby_state.error,target=lobby_state.target_error;
        const bool a=hide(menu,"lc_lobby_error_target",!(err && target)),b=hide(menu,"lc_lobby_error_copy",!(err && !target));
        hide(menu,"lc_lobby_error",!err || (target ? a : b));
        hide(menu,"lc_lobby_guest_left",!toast_on);
        bind_text(menu,"lc_toast_name",toast_on ? guest_toast.name.c_str() : "");
        // The service went away while the room stays: healthy P2P continues, only new players cannot join.
        const bool service_lost=broker_live() && broker_view().state==broker::phase::room && !broker_view().authenticated;
        hide(menu,"lc_lobby_service_lost",!service_lost);
    }
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
    lobby_focus_guard(menu);
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
    native_note_local_record();
    if(lobby_state.owner!=menu) { lobby_state.reset(menu);bindings.clear();pending.clear();roster.clear();guest_toast.clear(); }
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
    unsigned input=0;lobby_current_mask(input);input=lobby_screen_mask(menu,input); // pad X = Ready / Kick, Y (and RT) = Copy
    if(!(input&0x400010) && !lobby_state.copy_keyboard) lobby_state.copy_block=false;
    if((input&0x200000) && lobby_state.copied) lobby_state.copied=false; // Back (Esc / pad B) ends the informational "Code copied"
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
    if(native_lobby_rejected(menu)) return;
    if(!input_thread("lobby tick",false)) { lobby_original(menu,original);return; }
    try { if(broker_live() && broker_calls.tick) broker_calls.tick(); lobby_tick(menu,original,called); }
    catch(...) {
        pending.clear();bindings.clear();
        logger::write("online menus: lobby fields unavailable; native controls retained");
        if(!called) lobby_original(menu,original);
    }
}
void __fastcall host_tick(void* menu,void*) { lobby_frame(menu,0x5bacd0); }
void __fastcall join_tick(void* menu,void*) { lobby_frame(menu,0x5c8840); }
void __fastcall lobby_base_tick(void* menu,void*) {
    if(native_lobby_rejected(menu)) return;
    const bool previous=base_ui_dispatch;base_ui_dispatch=true;
    lobby_original(menu,0x5aaeb0);base_ui_dispatch=previous;
}
// The retail Host/Join per-frame updates (0x5babc0 / 0x5c8480) do two things: every 0.25 s they
// refresh the player list (0x5ba4e0 / 0x5c8150), then they draw the localized ONLINE_ADVICE
// paragraph ("To add additional players ...") in the classic font at 20,100 - over the skin's
// player cards (live 060p, shots/fr720-lobby-native-b.png). A skinned lobby keeps the list
// refresh with the same clock, timer and interval, and leaves the paragraph out. Both prologues
// are guarded byte for byte (online_menus_guards.hpp), so the replicated part is the retail one.
struct advice_free_update { unsigned original,timer,refresh; };
constexpr advice_free_update advice_free_updates[]{{0x5babc0,0x8a8288,0x5ba4e0},{0x5c8480,0x8afd14,0x5c8150}};
bool skinned_lobby_update(void* menu,unsigned original) {
    // A host notice (gameplay agreement: a joiner refused) replaces the paragraph for 60 s through the same
    // lookup; while one is showing, the retail update runs so the notice still reaches the host.
    if(gameplay_agreement::host_notice_showing(menu)) return lobby_original(menu,original);
    for(const auto& u:advice_free_updates) if(u.original==original) {
        __try {
            void* clock=at<void*(__cdecl*)()>(0x55b610)();
            const float now=(*reinterpret_cast<float(__thiscall* const*)(void*,int)>(*static_cast<std::uintptr_t*>(clock)+0x28))(clock,0);
            auto& next=*reinterpret_cast<volatile float*>(u.timer);
            if(next<now) {
                at<void(__thiscall*)(void*)>(u.refresh)(menu);
                next=now+*reinterpret_cast<const float*>(0x6819a0);
            }
            return true;
        }
        __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    return lobby_original(menu,original);
}
void lobby_refresh(void* menu,unsigned original) {
    if(native_lobby_rejected(menu)) return;
    const bool marked=lobby_live(menu);
    if(marked) { if(lobby_state.owner==menu) lobby_state.picker_safe=false;picker_visibility(menu,false); }
    if(marked) ++refreshing_names;
    if(marked) skinned_lobby_update(menu,original); else lobby_original(menu,original);
    if(marked) --refreshing_names;
    if(marked) lobby_after_refresh(menu);
}
void __fastcall host_refresh(void* menu,void*) { lobby_refresh(menu,0x5ba930); }
void __fastcall host_refresh_other(void* menu,void*) { lobby_refresh(menu,0x5ba940); }
void __fastcall host_update(void* menu,void*) { lobby_refresh(menu,0x5babc0); }
void __fastcall join_refresh(void* menu,void*) { lobby_refresh(menu,0x5c8620); }
void __fastcall join_update(void* menu,void*) { lobby_refresh(menu,0x5c8480); }
void lobby_activate(void* menu,int argument,unsigned original) {
    if(!native_lobby_permit(menu)) { native_lobby_recover();return; }
    if(!lobby_live(menu)) {
        if(lobby_native_activate(menu,argument,original)) {
            if(!native_lobby_entered(menu)) lobby_exit(menu);
        } else if(native_flow.phase!=native_phase::idle) native_flow_error("native_lobby_activation");
        return;
    }
    lobby_state.reset(menu);bindings.clear();pending.clear();roster.clear();guest_toast.clear();
    if(!lobby_lists(menu)) {
        logger::write("online menus: malformed lobby skin; returning to native online");
        if(native_flow.phase!=native_phase::idle) native_flow_error("native_lobby_skin");
        lobby_state.reset();lobby_exit(menu);return;
    }
    picker_visibility(menu,false);
    if(!lobby_native_activate(menu,argument,original) || !native_lobby_entered(menu)) {
        if(native_flow.phase!=native_phase::idle) native_flow_error("native_lobby_activation");
        lobby_state.reset();lobby_exit(menu);return;
    }
    lobby_after_refresh(menu);
    try { lobby_view view{};const bool valid=read_lobby(menu,view);bind_lobby(menu,view,valid); }
    catch(...) { pending.clear();bindings.clear();logger::write("online menus: activation fields unavailable; native controls retained"); }
}
void __fastcall host_activate(void* menu,void*,int arg) { lobby_activate(menu,arg,0x5ba870); }
void __fastcall join_activate(void* menu,void*,int arg) { lobby_activate(menu,arg,0x5c8590); }
int lobby_back(void* menu,unsigned original) {
    if(native_lobby_rejected(menu)) return 2;
    // A native Join tick/mouse Leave branch dispatches this same slot. It must
    // retain native Unready/Leave behavior even while Copy/error text is visible.
    const bool native_dispatch=(lobby_ticking && !base_ui_dispatch) || native_mouse;
    if(!native_dispatch && lobby_live(menu) && lobby_state.owner==menu &&
        (lobby_state.confirm || lobby_state.error)) {
        lobby_state.confirm=lobby_state.error=lobby_state.copied=lobby_state.target_error=false;pending.clear();
        restore_lobby_focus(menu);
        if(lobby_ticking) suppress_lobby_action=back_consumed=true;
        return 2;
    }
    // "Code copied" is informational (060q live: with its panel unseen the first Esc seemed to do nothing): Back clears it
    // and goes on to the native Back (the host's Leave confirm / the guest's Unready / Leave).
    if(!native_dispatch && lobby_live(menu) && lobby_state.owner==menu && lobby_state.copied) { lobby_state.copied=false;pending.clear(); }
    return lobby_native_back(menu,original);
}
int __fastcall host_back(void* menu,void*) { return lobby_back(menu,0x5aaff0); }
int __fastcall join_back(void* menu,void*) { return lobby_back(menu,0x5c8c60); }
void lobby_deactivate(void* menu,unsigned original) {
    if(native_lobby_skip_deactivate(menu)) return;
    if(lobby_state.owner==menu) { lobby_state.reset();bindings.clear();pending.clear(); }
    const bool departing=native_lobby_departing(menu);
    lobby_original(menu,original);
    if(departing) native_lobby_left();
}
void __fastcall host_deactivate(void* menu,void*) { lobby_deactivate(menu,0x5ba280); }
void __fastcall join_deactivate(void* menu,void*) { lobby_deactivate(menu,0x5c8070); }
// ---- Mouse hit boxes = the drawn focus bars (online_menus_hits_rules.hpp; 060p live item 7) -------------------------------
// The builder's Data/lc_online_hits.txt, read once per mod generation on the first scoped mouse message. Absent or
// malformed: every screen keeps the engine's own hit test (a build that predates the table, or a refused file).
namespace hits = online_menus_hits;
hits::table hit_table;
unsigned hit_generation=0;
bool (*pointer_probe)(int&,int&,int&,int&)=nullptr; // display: the pointer message's engine-screen pixel (exports.cpp)
bool hits_ready() {
    try {
        const auto generation=mod_loader::generation();
        if(!generation) return false;
        if(generation!=hit_generation) {
            hit_generation=generation; hit_table.entries.clear();
            const auto path=std::filesystem::path(mod_loader::game_root())/L"Data/lc_online_hits.txt";
            std::ifstream file(mod_loader::resolve_file(path.wstring()),std::ios::binary);
            if(!file) { logger::write_once("online:hits-absent","online menus: no Data/lc_online_hits.txt (a package from before the table); the mouse hit boxes of the co-op screens are OFF and the engine's hit test stays"); return false; }
            std::string text(hits::max_file+1,'\0');
            file.read(text.data(),std::streamsize(text.size()));
            text.resize(std::size_t(file.gcount()));
            std::string why;
            if(text.size()>hits::max_file || !hits::parse(text,hit_table,why)) {
                hit_table.entries.clear();
                logger::write_once("online:hits-refused","online menus: ERROR: Data/lc_online_hits.txt REFUSED (%s); the mouse hit boxes of the co-op screens are OFF - the engine's own hit test (dead strips above and beside the bars) stays; rebuild the package",why.empty() ? "larger than 256 KiB" : why.c_str());
                return false;
            }
            logger::write_once("online:hits","online menus: mouse hit boxes = drawn focus bars (%s, %u entries)",hits::contract,unsigned(hit_table.entries.size()));
        }
        return !hit_table.entries.empty();
    } catch(...) { hit_table.entries.clear(); return false; }
}
void* item_try(void* menu,const char* name) {
    __try { return item(menu,name); } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
// The engine's own tests on an item: focusable (+0x54 bit 3) and shown (vt+4 = +0x54 bit 2 clear for text/model items), then
// its rectangle fields (0x5bc530): both boxes the engine's passes ask (hits::item_rects).
struct focusable { void* item=nullptr; hits::item_rects box; bool shown=false; };
bool focusable_of(void* it,focusable& out) {
    __try {
        auto* p=static_cast<unsigned char*>(it);
        if(!(p[0x54]&8)) return false;
        out.item=it; out.shown=!at<bool(__thiscall*)(void*)>((*reinterpret_cast<std::uintptr_t**>(it))[1])(it);
        std::int16_t f[6]; std::memcpy(f,p+0x70,sizeof f);
        out.box=hits::engine_rects(f[0],f[1],f[2],f[3],f[5]); return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// An item's boxes whatever its focus / shown state (text_playername1: the lobbies' mouse asks its first-pass box regardless).
bool boxes_of(void* it,focusable& out) {
    __try {
        std::int16_t f[6]; std::memcpy(f,static_cast<unsigned char*>(it)+0x70,sizeof f);
        out.item=it; out.shown=true; out.box=hits::engine_rects(f[0],f[1],f[2],f[3],f[5]); return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// EVERY item the engine's mouse walk reaches: 0x5ae0a0 walks the menu's item tree (container at menu+0x824: nodes ordered by
// the item name's key, node n at +0xc + 16 n, root index at +4, item pointers at +0xde8) in order - leftmost node (0x585be0),
// then successors (0x585c10) - and the first item whose vt+0x74 answers wins. Its order is the name keys' order, not ours.
bool engine_items(void* menu,std::vector<void*>& out) {
    __try {
        auto* list=static_cast<char*>(menu)+0x824;
        constexpr int end=0x3fffffff;
        int node=*reinterpret_cast<int*>(list+4);
        if(node==end) return true;
        if(const int left=*reinterpret_cast<int*>(list+16*(node+1));left!=end) node=at<int(__thiscall*)(void*,int)>(0x585be0)(list,left);
        for(int guard=0;node!=end && guard<1024;++guard) {
            if(auto* it=*reinterpret_cast<void**>(list+0xde8+4*node)) out.push_back(it);
            node=at<int(__thiscall*)(void*,int)>(0x585c10)(list,node);
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void* focus_of(void* menu);
// 060q live (failure 11): the Hub's row hints stayed light blue on the focused amber bar and the help box always read
// HOST A GAME. LC authors, per row, a navy twin of the hint (<row>_hint_on) and the row's help lines (lc_help_<row>_title /
// _1 / _2, all hidden but the Host row's); the DLL shows the focused row's. Focus elsewhere (Change name, Rejoin, a prompt)
// keeps the last row's help. Assets without the twins (an older LC) keep the plain hint and the authored help.
constexpr const char* hub_rows[]{"host","code","browse","party","other"};
std::string hub_help_row="host";
void hub_focus_follow(void* menu) {
    // A new Hub visit (the binding cache is cleared on every departure and scoped to the menu) starts on the Host row's
    // help, as authored - never on the previous visit's row while the CMenu focus is still unset.
    bindings.scope(menu);
    if(!bindings.visibility.count("host_hint_on")) hub_help_row="host";
    void* const focused=focus_of(menu);
    std::string row;
    for(const auto* r:hub_rows) if(focused && item_try(menu,r)==focused) row=r;
    // A row without its own help set (the Party row is not authored one) keeps the last row's help.
    if(!row.empty() && item_try(menu,("lc_help_"+row+"_title").c_str())) hub_help_row=row;
    for(const auto* r:hub_rows) {
        const std::string key(r);
        const bool on=row==key;
        if(hide(menu,(key+"_hint_on").c_str(),!on)) hide(menu,(key+"_hint").c_str(),on);
        const bool help=hub_help_row==key;
        for(const auto* part:{"_title","_1","_2"}) hide(menu,("lc_help_"+key+part).c_str(),!help);
    }
}
void* focus_of(void* menu) {
    // CMenu's focused item (+0x324: 0x5ae0a0 compares the hit item with it before setFocus).
    __try { return *reinterpret_cast<void**>(static_cast<char*>(menu)+0x324); } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
void menu_name(void* menu,char (&name)[64]) {
    __try { std::memcpy(name,static_cast<char*>(menu)+0xc,63); name[63]=0; } __except(EXCEPTION_EXECUTE_HANDLER) { name[0]=0; }
}
// (x, y): the engine's view point. True with (rx, ry) the point to hand the engine instead; false: pass it on unchanged.
// The point is a half-view-unit estimate unless the window procedure's pixel is known (logged once per reason).
bool scoped_point(void* menu,int x,int y,int& rx,int& ry) {
    try {
        if(!hits_ready()) return false;
        char name[64]{}; menu_name(menu,name);
        if(!name[0]) return false;
        hits::view_point p=hits::from_engine(x,y);
        int px=0,py=0,sw=0,sh=0,ex=0,ey=0;
        if(!pointer_probe) logger::write_once("online:hits-half-noprobe","online menus: mouse hit boxes: no window procedure pixel (display Mode unset); the point is known to half a view unit only");
        else if(!pointer_probe(px,py,sw,sh)) logger::write_once("online:hits-half-nomsg","online menus: mouse hit boxes: a mouse message without a window procedure pixel (wheel, or no client point); half a view unit");
        else if(!hits::engine_point_of_pixel(px,py,sw,sh,ex,ey) || ex!=x || ey!=y) logger::write_once("online:hits-half-mismatch","online menus: mouse hit boxes: the window procedure pixel does not match the engine's point (%d,%d vs %d,%d); half a view unit",ex,ey,x,y);
        else p=hits::from_pixel(px,py,sw,sh);
        // Everything the engine's walk asks, listed or not.
        std::vector<void*> walked;
        if(!engine_items(menu,walked)) return false;
        std::vector<focusable> all;
        for(void* it:walked) { focusable f; if(focusable_of(it,f)) all.push_back(f); }
        // The listed items (the table's rows for this menu) that can be a target now: shown and focusable.
        struct target { focusable f; const hits::bar* rect; };
        std::vector<target> active;
        std::vector<void*> listed;
        for(const auto& e:hit_table.entries) {
            if(e.menu!=name) continue;
            void* it=item_try(menu,e.item.c_str());
            if(!it) continue;
            listed.push_back(it);
            focusable f;
            if(focusable_of(it,f) && f.shown) active.push_back({f,&e.rect});
        }
        auto in=[](const std::vector<void*>& v,void* it) { return std::find(v.begin(),v.end(),it)!=v.end(); };
        // Focusable items the table does not list (the Kick picker's rows, anything else the walk reaches), plus the lobbies'
        // text_playername1, whose first-pass box 0x5ba2b0 / 0x5c8da0 ask whatever its shown / focus state.
        std::vector<focusable> unlisted;
        for(const auto& f:all) if(!in(listed,f.item)) unlisted.push_back(f);
        if(void* picker=item_try(menu,"text_playername1"); picker && !in(listed,picker) &&
           std::none_of(unlisted.begin(),unlisted.end(),[&](const focusable& u) { return u.item==picker; })) {
            focusable f; if(boxes_of(picker,f)) unlisted.push_back(f);
        }
        // Bars can overlap (the prompt row's bars reach 12 units into their neighbours), but only the focused item's bar
        // is drawn: it wins inside its bar; elsewhere the bar whose own text box is nearest the point (hits::choose).
        void* const focused=focus_of(menu);
        std::vector<hits::candidate> held;
        for(const auto& a:active) if(hits::inside(*a.rect,p)) held.push_back({a.f.item==focused,a.f.box.shifted});
        if(const auto pick=hits::choose(held,p);pick>=0) {
            std::size_t seen=0;const target* c=nullptr;
            for(const auto& a:active) if(hits::inside(*a.rect,p) && seen++==std::size_t(pick)) { c=&a;break; }
            // No other focusable item - walked or not, shown or not - may answer for the point we hand over; if every point
            // is taken, only the shown ones count (the base walk skips a hidden item).
            for(const bool shown_only:{false,true}) {
                std::vector<hits::item_rects> others;
                for(const auto& o:all) if(o.item!=c->f.item && (o.shown || !shown_only)) others.push_back(o.box);
                for(const auto& o:unlisted) if(o.item!=c->f.item && (o.shown || !shown_only) &&
                    std::none_of(all.begin(),all.end(),[&](const focusable& a) { return a.item==o.item; })) others.push_back(o.box);
                if(hits::free_point(c->f.box,others,rx,ry)) return true;
            }
            return false;
        }
        // Outside every bar. An item the table does not list keeps the point; a listed item's own box or first-pass box (a dead
        // strip the bar does not cover) gets nothing: no hover, no click.
        for(const auto& u:unlisted) if(hits::holds_any(u.box,x,y)) return false;
        for(const auto& a:active) if(hits::holds_any(a.f.box,x,y)) { rx=hits::nowhere_x; ry=hits::nowhere_y; return true; }
        return false;
    } catch(...) { return false; }
}
using mouse_fn=std::uint32_t(__thiscall*)(void*,int,int,int,int,int);
std::uint32_t __fastcall menu_mouse(void* menu,void*,int x,int y,int flags,int message,int extra) {
    int rx=x,ry=y;
    if(ready && custom(which(menu)) && !scoped_point(menu,x,y,rx,ry)) { rx=x; ry=y; }
    return at<mouse_fn>(0x5ae0a0)(menu,rx,ry,flags,message,extra);
}
bool lobby_mouse_native(void* menu,unsigned original,int x,int y,int flags,int message,int extra) {
    __try { return at<bool(__thiscall*)(void*,int,int,int,int,int)>(original)(menu,x,y,flags,message,extra); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool lobby_mouse(void* menu,unsigned original,int x,int y,int flags,int message,int extra) {
    if(native_lobby_rejected(menu)) return true;
    // The skinned lobbies' rows and prompts hit-test against their drawn bars too (an unskinned lobby has no table rows).
    if(int rx=x,ry=y;lobby_live(menu) && scoped_point(menu,x,y,rx,ry)) { x=rx; y=ry; }
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
        "lc_notice_profile_save","lc_notice_name_open","lc_notice_recent_save","lc_notice_admission",
        // Round 15: native discovery / admission refusal (with and without rule names) / service loss / timeout, clipboard.
        "lc_notice_discovery","lc_notice_refused","lc_notice_refusal_rules","lc_notice_service_lost","lc_notice_timeout","lc_notice_clipboard",
        // 0.6 preview fix E: the host removed this guest (its native Kick).
        "lc_notice_removed"};
    bool found=false;
    for(const auto* item:notices) {
        const bool selected=error_visible && feedback==item;
        const bool usable=hide(menu,item,!selected);
        if(selected && usable) found=true;
    }
    hide(menu,"lc_inline_error",!error_visible || found);
    bind_text(menu,"lc_refusal_rules",error_visible && feedback=="lc_notice_refusal_rules" ? native_rules.c_str() : "");
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
// Whether the hub's player is shown the pad (pad_prompts' device tracker; wired by exports.cpp). Without it - Prompts=off,
// the prompts patch refused, a host fixture - the raw bits are read as they were.
unsigned screen_mask(unsigned raw) {
    return pad_probe && pad_probe(menu_player) ? pad_menu_mask(raw) : raw;
}
void schedule_fixture(bool retry=false) {
    if(broker_live()) {
        if(resolve_edit && (resolve_edit!=code.generation || code.text.size()!=8)) broker_cancel(broker_view().state==broker::phase::room);
        const auto state=broker_view().state;
        if(state==broker::phase::authenticating || state==broker::phase::resolving || state==broker::phase::room ||
            state==broker::phase::canceling || state==broker::phase::leaving) return;
        const auto ticket=code.resolve(retry);
        if(ticket && broker_calls.resolve) {
            if(broker_calls.resolve(code.text)) { resolve_edit=ticket;resolve_ticket=broker_view().generation;error_visible=false; }
            else if(broker_view().state!=broker::phase::refused) code.submitted=0; // defer only retryable starts
        }
        return;
    }
    const auto ticket=code.resolve(retry);
    if(ticket && offline_fixture) { fixture_ticket=ticket; fixture_code=code.text; message("lc_notice_resolving"); }
    else if(ticket) message("lc_notice_unavailable");
}
void code_input(int value) {
    switch(static_cast<intent>(value)) {
    case intent::paste: {
        clipboard_api api;std::string text;
        const auto result=read_clipboard_result(api,text);
        if(result==clip_result::ok) { code.replace(text);error_visible=false; }
        else message(result==clip_result::unavailable ? "lc_notice_clipboard" : result==clip_result::bad_symbols ? "lc_notice_code_invalid" : "lc_notice_paste_invalid");
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

// ---- Round 8: the in-session Party screen (lc_party, a base CMenu opened from the pause menu's lc_party_link) -------------
// Presentation + intents only. The room, readiness, admission, the transport and the native session stay the owners of
// their facts; Resume/Team/Codex/Options/Quit are native routes (the rail's own usecmds, or the base Back); Leave hands
// the native quit command to the console once; Kick asks the transport for the agreed departure once. Nothing a peer sends
// authorizes any of it: there is no party wire message for removal (a new peer contract would need a rule id).
party_model party_ui;
std::uintptr_t party_owner=0;
std::string party_origin;      // the item to focus again when a confirmation closes
int party_feedback=0;          // 1 copied, 2 copy failed
DWORD party_feedback_at=0;
DWORD party_departed_at=0;     // a Leave was handed to the native quit; cleared by Back or after party_departed_ms
constexpr DWORD party_departed_ms=4000;
constexpr DWORD party_feedback_ms=6000;
struct party_card { party_row row; int ping=-1; bool to_host=false; int route=0; int build=0; int phase=-1; int percent=-1; };
struct party_snapshot_view { std::vector<party_card> cards; party_roster roster; std::string code; };
party_snapshot_view party_collect() {
    party_snapshot_view out;
    lobby_fixture extra{};bool facts=false;
    if(lobby_fields) facts=lobby_fields(extra) && extra.count<=4 && extra.pending_count<=3;
    else if(broker_live() && broker_calls.party) { facts=broker_calls.party(extra) && extra.count<=4 && extra.pending_count<=3; if(!facts) extra={}; }
    if(!facts) {
        if(broker_live() && broker_view().state==broker::phase::room && broker_view().code.size()==8) out.code=broker_view().code;
        return out;
    }
    const auto end=std::find(std::begin(extra.code),std::end(extra.code),char(0));
    if(end!=std::end(extra.code)) if(auto parsed=parse_code({extra.code,std::size_t(end-extra.code)})) out.code=*parsed;
    out.roster.hosting=extra.hosting;out.roster.session=extra.session;
    std::vector<const lobby_fixture::member*> natives;
    for(unsigned i=0;i<extra.count;++i) natives.push_back(&extra.members[i]);
    std::sort(natives.begin(),natives.end(),[](const auto* a,const auto* b) { return a->native_index<b->native_index; });
    for(const auto* m:natives) {
        if(out.cards.size()==4) break;
        party_card c;
        c.row.index=int(m->native_index);c.row.self=m->self;c.row.host=m->host;
        c.row.name=display(std::string(m->name,strnlen(m->name,31)),31);
        if(c.row.name.empty()) c.row.name="Player";
        c.ping=m->self ? -1 : m->ping_ms;c.to_host=m->ping_to_host;c.route=m->route;
        c.build=m->build>=0 ? m->build : m->same_build ? 2 : 0;c.phase=m->phase;c.percent=m->percent;
        out.cards.push_back(c);
    }
    for(unsigned i=0;i<extra.pending_count && out.cards.size()<4;++i) {
        const auto& m=extra.pending[i];
        party_card c;
        c.row.pending=true;
        c.row.name=m.name[0] ? display(std::string(m.name,strnlen(m.name,31)),31) : std::string("Player");
        c.ping=m.ping_ms;c.to_host=m.ping_to_host;c.route=m.route;c.build=m.build;c.phase=m.phase;
        out.cards.push_back(c);
    }
    for(const auto& c:out.cards) out.roster.rows.push_back(c.row);
    return out;
}
bool party_native_back(void* menu) {
    __try { at<tick_fn>(0x5ad7c0)(menu);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// The native quit route the pause menu's own Quit uses (its usecmd, queued through the console). Called once per Leave.
bool party_native_leave() {
    __try {
        auto* console=at<getter>(0x55c890)();
        if(!console || *static_cast<std::uint32_t*>(console)!=0x69a81c) return false;
        return at<command_fn>(0x55c410)(console,"mainmenuexit");
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
int party_tone(int phase) {
    switch(phase) { case 2: case 3: case 7: return 0; case 0: case 1: case 4: case 5: case 6: return 1; default: return 2; }
}
void party_focus(void* menu,const std::string& item) { if(!item.empty()) lobby_focus(menu,item.c_str()); }
void party_frame(void* menu) {
    const auto id=reinterpret_cast<std::uintptr_t>(menu);
    if(party_owner!=id) { party_ui.reset();party_owner=id;party_origin.clear();party_feedback=0;bindings.clear();pending.clear();keyboard_input.clear(); }
    if(broker_live() && broker_calls.tick) broker_calls.tick();
    native_note_local_record();
    const auto view=party_collect();
    const auto& roster=view.roster;
    // The queued native quit may never run (refused/blocked): the screen must not stay frozen. A short timeout clears the latch.
    if(party_ui.departed && now_ms()-party_departed_at>=party_departed_ms) {
        party_ui.departed=false;
        logger::write("online menus: party Leave: the native quit did not take effect in %lu ms; the screen is usable again",(unsigned long)party_departed_ms);
    }
    std::string want_focus; // applied AFTER the bindings below: a hidden/disabled item cannot take focus
    const bool open_before=party_ui.confirm!=party_confirm::none;
    party_ui.refresh(roster);
    if(open_before && party_ui.confirm==party_confirm::none && !party_ui.departed) want_focus="*origin";
    const int action=pending.take(id,screen::party);
    if(action) {
        const auto result=party_ui.apply(action,roster);
        switch(result.act) {
        case party_act::native_back: {
            const bool ok=party_native_back(menu);
            party_ui.reset();pending.clear();
            if(!ok) logger::write("online menus: party Back faulted; the pause menu is retained");
            return; // never bind after requesting a close
        }
        case party_act::copy: {
            const bool copied=!view.code.empty() && clipboard_copy(view.code.substr(0,4)+"-"+view.code.substr(4));
            party_feedback=copied ? 1 : 2;party_feedback_at=now_ms();
        } break;
        case party_act::opened_leave: party_origin="lc_party_leave";want_focus="lc_party_cancel";break;
        case party_act::opened_kick:
            party_origin="lc_p"+std::to_string(action-int(intent::party_kick_first)+1)+"_kick";want_focus="lc_party_cancel";break;
        case party_act::dismissed: want_focus="*origin";break;
        case party_act::leave_run: {
            const bool ok=party_native_leave();
            logger::write("online menus: party Leave -> native quit route %s",ok ? "queued once" : "refused");
            if(!ok) { party_ui.departed=false;want_focus="*origin"; } // nothing was handed over; the player may try again
            else party_departed_at=now_ms();
            if(party_origin.empty()) party_origin="lc_party_leave";
        } break;
        case party_act::kick_run: {
            const bool ok=broker_calls.remove_member && broker_calls.remove_member(unsigned(result.kick_index));
            logger::write("online menus: party Kick of native index %d -> agreed departure %s",result.kick_index,ok ? "started once" : "refused");
            if(!ok) party_ui.kicked&=~(1u<<result.kick_index);
            want_focus="*origin";
        } break;
        case party_act::stale: case party_act::refused:
            // A stale/refused Yes closed (or never had) its confirmation: focus must not stay on a button that just got hidden.
            if(open_before && party_ui.confirm==party_confirm::none) want_focus="*origin";
            break;
        default: break;
        }
    }
    const bool confirm_leave=party_ui.confirm==party_confirm::leave,confirm_kick=party_ui.confirm==party_confirm::kick;
    const bool open=confirm_leave || confirm_kick;
    // Room header, invite code.
    if(broker_live() && broker_view().state==broker::phase::room) {
        const auto& state=broker_view();
        if(!state.room_name.empty()) bind_text(menu,"lc_room_name",display(state.room_name,32).c_str());
        if(state.room_metadata_known) bind_text(menu,"lc_room_visibility",state.public_room ? "Public" : "Private");
    }
    const std::string shown=view.code.size()==8 ? view.code.substr(0,4)+"-"+view.code.substr(4) : std::string("---- ----");
    bind_text(menu,"lc_room_code",shown.c_str());
    // Cards.
    std::string notice_name;int notice_phase=-1,notice_percent=-1;
    for(unsigned i=0;i<4;++i) {
        const auto prefix="lc_p"+std::to_string(i+1)+"_";
        const bool present=i<view.cards.size();
        for(const auto* field:{"empty","empty_title","empty_help","empty_plus"}) hide(menu,(prefix+field).c_str(),present);
        for(const auto* field:{"card","name","slot"}) hide(menu,(prefix+field).c_str(),!present);
        if(!present) {
            for(const auto* field:{"host","you","hero_fallback","hero_next","ms","measuring","ping","to_host","same_build","checking_build","other_build",
                    "pending","selected","tone_good","tone_warn","tone_idle","kick"}) hide(menu,(prefix+field).c_str(),true);
            enable(menu,(prefix+"kick").c_str(),false);
            bind_text(menu,(prefix+"phase").c_str(),"");bind_text(menu,(prefix+"route").c_str(),"");
            continue;
        }
        const auto& c=view.cards[i];
        bind_text(menu,(prefix+"name").c_str(),c.row.name.c_str());
        bind_text(menu,(prefix+"slot").c_str(),("P"+std::to_string(i+1)).c_str());
        hide(menu,(prefix+"host").c_str(),!c.row.host);
        hide(menu,(prefix+"you").c_str(),!c.row.self);
        hide(menu,(prefix+"pending").c_str(),!c.row.pending);
        const bool next=c.phase==4 || c.phase==5;
        hide(menu,(prefix+"hero_next").c_str(),!next);hide(menu,(prefix+"hero_fallback").c_str(),next);
        const bool measured=c.ping>=0 && c.ping<=60000 && !c.row.self;
        bind_text(menu,(prefix+"ping").c_str(),measured ? std::to_string(c.ping).c_str() : "");
        hide(menu,(prefix+"ping").c_str(),!measured);hide(menu,(prefix+"ms").c_str(),!measured);
        hide(menu,(prefix+"measuring").c_str(),measured || c.row.self);
        hide(menu,(prefix+"to_host").c_str(),!(measured && c.to_host));
        bind_text(menu,(prefix+"route").c_str(),measured ? route_text(c.route) : "");
        const int stage=c.row.self ? 2 : c.build;
        hide(menu,(prefix+"same_build").c_str(),!(stage==2 || stage==3));
        hide(menu,(prefix+"checking_build").c_str(),stage==2 || stage==3 || stage==1);
        hide(menu,(prefix+"other_build").c_str(),stage!=1);
        bind_text(menu,(prefix+"phase").c_str(),phase_text(c.phase));
        const int tone=party_tone(c.phase);
        hide(menu,(prefix+"tone_good").c_str(),tone!=0);hide(menu,(prefix+"tone_warn").c_str(),tone!=1);hide(menu,(prefix+"tone_idle").c_str(),tone!=2);
        const bool kick=!open && !party_ui.departed && roster.kickable(i);
        hide(menu,(prefix+"kick").c_str(),!kick);enable(menu,(prefix+"kick").c_str(),kick);
        hide(menu,(prefix+"selected").c_str(),!(confirm_kick && !c.row.pending && c.row.index==party_ui.kick_index));
        if(notice_name.empty() && !c.row.self && (c.phase==4 || c.phase==5)) { notice_name=c.row.name;notice_phase=c.phase;notice_percent=c.percent; }
    }
    // Notice bar: one pending member at a time; its text nodes are hidden with the scene.
    const bool waiting=!notice_name.empty() && notice_phase==4,receiving=!notice_name.empty() && notice_phase==5;
    hide(menu,"lc_party_waiting",!waiting);hide(menu,"lc_party_receiving",!receiving);
    hide(menu,"lc_notice_name",notice_name.empty());
    bind_text(menu,"lc_notice_name",notice_name.c_str());
    const bool percent_ok=receiving && notice_percent>=0 && notice_percent<=100;
    hide(menu,"lc_notice_percent",!percent_ok);
    bind_text(menu,"lc_notice_percent",percent_ok ? (std::to_string(notice_percent)+"%").c_str() : "");
    // Propose load: visibly unavailable until round 14; the item itself is authored disabled and never enabled here.
    enable(menu,"lc_party_propose",false);
    hide(menu,"lc_party_propose_soon",!roster.hosting);hide(menu,"lc_party_propose_host_only",roster.hosting);
    // Feedback (six seconds).
    if(party_feedback && now_ms()-party_feedback_at>=party_feedback_ms) party_feedback=0;
    hide(menu,"lc_party_copied",party_feedback!=1);hide(menu,"lc_party_copy_failed",party_feedback!=2);
    // Confirmations: one scene, its own buttons; everything else is disabled while one is open so navigation cannot leave it.
    hide(menu,"lc_party_confirm_leave_host",!(confirm_leave && roster.hosting));
    hide(menu,"lc_party_confirm_leave_guest",!(confirm_leave && !roster.hosting));
    hide(menu,"lc_party_confirm_kick",!confirm_kick);
    bind_text(menu,"lc_kick_name",confirm_kick ? display(party_ui.kick_name,31).c_str() : "");
    hide(menu,"lc_kick_name",!confirm_kick);
    // ONE shared Yes (intent 67) for both confirmations; lc_party_kick_yes (69) is no longer authored (absent item = no-op).
    hide(menu,"lc_party_leave_yes",!open);enable(menu,"lc_party_leave_yes",open);
    hide(menu,"lc_party_cancel",!open);enable(menu,"lc_party_cancel",open);
    const bool idle=!open && !party_ui.departed;
    for(const auto* item:{"lc_rail_resume","lc_rail_team","lc_rail_party","lc_rail_codex","lc_rail_options","lc_rail_quit","lc_party_leave"}) enable(menu,item,idle);
    enable(menu,"lc_party_invite",idle && view.code.size()==8);
    if(!want_focus.empty()) {
        // Back to the item that opened the confirmation when it is usable now, else the invite button, else the current page.
        std::string target=want_focus;
        if(target=="*origin") {
            target=party_origin;
            const bool kick_origin=target.size()>3 && target.compare(0,4,"lc_p")==0 && target.back()=='k';
            if(kick_origin) {
                const auto card=std::size_t(target[4]-'1');
                if(card>=roster.rows.size() || !roster.kickable(card) || ((party_ui.kicked>>roster.rows[card].index)&1)) target.clear();
            } else if(target!="lc_party_leave") target.clear();
            if(target.empty()) target=view.code.size()==8 ? "lc_party_invite" : "lc_rail_party";
            if(party_ui.departed) target.clear();
            party_origin.clear();
        }
        party_focus(menu,target);
    }
}
// Native menu history: manager +0x86094, ten 0x8c-byte entries (name at +0), depth at +0x86610. Opening a menu
// pushes the outgoing one unless that name is already stacked (0x5d74d0); closing one without a deferred next pops
// to the newest entry whose name differs (0x5d7080, e.g. text_entry Enter / native Back). The LC screens move by
// forward requests, so a screen we come back to can still be stacked under the native menus opened above it: after a
// failed hub Host the stack was [main, lc_online, lc_host, game_options] while the Hub was current, and Change name ->
// Enter popped to game_options (live 060p x4). While a custom screen is current its own entry and everything above it
// are stale - exactly what a native Back to it would have removed - so trim the depth to that entry.
int native_history_trim_impl(void* menu) {
    __try {
        auto* m=static_cast<unsigned char*>(manager());
        if(!m || *reinterpret_cast<std::uint32_t*>(m)!=0x6a236c || !menu) return -1;
        auto& depth=*reinterpret_cast<int*>(m+0x86610);
        if(depth<=0 || depth>10) return -1;
        const auto* name=static_cast<const char*>(menu)+0xc;
        for(int i=0;i<depth;++i) {
            const auto* entry=reinterpret_cast<const char*>(m+0x86094+i*0x8c);
            if(!std::strncmp(entry,name,63)) { const int dropped=depth-i;depth=i;return dropped; }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { }
    return 0;
}
void native_history_trim(void* menu) {
    const int dropped=native_history_trim_impl(menu);
    if(dropped>0) logger::write("online menus: native menu history trimmed at %s (%d stale entr%s)",
        static_cast<const char*>(menu)+0xc,dropped,dropped==1 ? "y" : "ies");
}
void tick_impl(void* menu,bool& native_called) {
    if(ready && live(menu)) {
        // The native mask is rising-edge input (controller +2f8 XOR +374).
        // Keyboard text already posted by DirectInput wins over X/Y bindings.
        const auto mask=screen_mask(input_mask());
        if(!(mask&0x200000)) back_held=false;
        const auto s=which(menu);
        if(!pending.action && !keyboard_input.count) {
            int shortcut=0;
            if(s==screen::code) {
                if(mask&0x400000) shortcut=int(intent::paste); // MENU_NEXT: pad Y (screen_mask), RT, keyboard KP5
                else if(mask&0x1000000) shortcut=int(intent::clear); // MENU_OTHER: pad X (screen_mask), keyboard Del
                else if(mask&0x100000) shortcut=int(intent::done); // MENU_OK (Start)
            } else if(s==screen::hub) {
                if(mask&0x1000000) shortcut=int(intent::edit_name);
                else if(mask&0x400000) shortcut=int(intent::rejoin);
            } else if(s==screen::party) { if(mask&0x400000) shortcut=int(intent::party_invite); // MENU_NEXT (Y)
            } else if(s==screen::browse && (mask&0x400000)) shortcut=int(intent::refresh);
            else if(s==screen::host && (mask&0x100000)) shortcut=int(intent::open_room);
            if(shortcut) pending.post(shortcut,reinterpret_cast<std::uintptr_t>(menu),s);
        }
    }
    native_called=true;
    at<tick_fn>(0x5aaeb0)(menu);
    if(tick_fault) tick_fault();
    if(!ready || !live(menu)) { pending.clear();keyboard_input.clear();if(broker_live() && broker_view().state!=broker::phase::room) broker_cancel();return; }
    const auto s=which(menu);
    if(s==screen::party) { party_frame(menu);return; }
    native_history_trim(menu);
    broker_scope(s);
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
    if(s==screen::hub && !int(action) && broker_live() && broker_calls.take_preset) {
        broker::launch_preset preset;
        if(broker_calls.take_preset(preset)) {
            if(preset.action==broker::operation::host) { host_room_name=preset.name;host_public=preset.public_room;destination="lc_host"; }
            else if(preset.action==broker::operation::resolve) { rejoin_seed=preset.code;destination="lc_code"; }
        }
    }
    // The host-left panel is modal over the Hub: only Host again (the Host intent) and Back/Dismiss act; every other posted
    // action is ignored (a stale or doubled report changes nothing).
    const bool modal=s==screen::hub && host_left_state.active;
    switch(action) {
    case intent::host: if(s==screen::hub) { host_left_state.dismiss();destination="lc_host"; } break;
    case intent::code: if(s==screen::hub && !modal) destination="lc_code"; break;
    case intent::browse: if(s==screen::hub && !modal) destination="lc_browse"; break;
    case intent::other: if(s==screen::hub && !modal) destination="online"; break;
    case intent::back:
        if(s==screen::hub && host_left_state.active) host_left_state.dismiss();
        else if(s==screen::code && !code.text.empty()) { code.erase(); error_visible=false; }
        else destination=s==screen::hub ? "main" : "lc_online";
        break;
    case intent::edit_name: if(s==screen::hub && !modal && begin_name()) return; break;
    case intent::paste: case intent::erase: case intent::clear: case intent::done: case intent::invalid_key:
        if(s==screen::code) code_input(int(action));break;
    case intent::rejoin: if(s==screen::hub && recent && !modal) {
        rejoin_seed=recent->code;destination="lc_code";
    } break;
    case intent::refresh:
        browse_join_code.clear(); // a Refresh drops a join still waiting for the lookup limiter
        if(s==screen::browse && broker_live() && broker_calls.list && broker_calls.list()) { last_list_at=now_ms();listed_started=true; } break;
    case intent::open_room:
        if(s==screen::host && broker_live()) { native_host_begin(); return; } break;
    case intent::visibility: if(s==screen::host && native_flow.phase==native_phase::idle) host_public=!host_public; break;
    case intent::retry:
        if(broker_live() && broker_calls.retry && broker_calls.retry() && s==screen::code) {
            resolve_ticket=broker_view().generation;resolve_edit=code.generation;code.submitted=code.generation;
        } break;
    case intent::previous: if(s==screen::browse) { browse_selected=browse_selected>=browse_rows ? browse_selected-browse_rows : 0; browse_code.clear(); } break;
    case intent::next: if(s==screen::browse && !broker_view().rows.empty()) { browse_selected=std::min<unsigned>(browse_selected+browse_rows,unsigned(broker_view().rows.size()-1));browse_code.clear(); } break;
    case intent::join_selected: if(s==screen::browse) browse_join("Join prompt"); break;
    case intent::unavailable: message("lc_notice_unavailable"); break;
    case intent::dismiss: error_visible=false;if(s==screen::hub) host_left_state.dismiss();break;
    default:
        if(s==screen::browse && int(action)>=int(intent::row_first) && int(action)<int(intent::row_first)+int(browse_rows)) {
            const auto selected=browse_offset+unsigned(int(action)-24);
            if(selected<broker_view().rows.size()) {
                // Enter / pad A / a click on the selected room joins it ("[Enter] Join"); the selection follows focus (keys,
                // pad and mouse hover move focus first), so the room under the focus bar is the one joined. A command for
                // another row (no focus over it, e.g. a script) selects that row first.
                const bool again=selected==browse_selected && browse_code==broker_view().rows[selected].code;
                browse_selected=selected; browse_code=broker_view().rows[selected].code;
                if(again) browse_join("row");
            }
        }
        if(s==screen::code && int(action)>=32 && int(action)<=62) {
            code.append(alphabet[int(action)-32]); error_visible=false;
        }
        break;
    }
    // Apply this tick's edit/cancel BEFORE accepting a prior completion.
    if(completed_ticket && code.complete(completed_ticket) && completed_code==code.text && s==screen::code && !destination) {
        message(remember_fixture() ? "lc_notice_fixture" : "lc_notice_recent_save");
    }
    if(s==screen::code) schedule_fixture(); // invalidate edits before draining worker completions
    if(destination && broker_live()) broker_cancel(broker_view().state==broker::phase::room);
    if(!destination) { browse_join_tick(s); broker_pump(s); }
    else if(broker_live() && broker_calls.tick) broker_calls.tick();
    if(s==screen::code && broker_live() && broker_view().state==broker::phase::room && resolve_ticket==broker_view().generation && code.complete(resolve_edit) && remembered_ticket!=resolve_ticket) {
        remembered_ticket=resolve_ticket;
        if(!remember_game(broker_view().code,broker_view().room_name.empty() ? "Online game" : broker_view().room_name)) message("lc_notice_recent_save");
    }
    if(s==screen::browse && broker_live() && broker_view().state==broker::phase::room && remembered_ticket!=broker_view().generation) {
        remembered_ticket=broker_view().generation;
        if(!remember_game(broker_view().code,broker_view().room_name.empty() ? "Online game" : broker_view().room_name)) message("lc_notice_recent_save");
    }
    if(!destination && native_flow_tick(s)) return;
    if(destination) {
        if(s==screen::hub) host_left_state.dismiss(); // leaving the Hub by any route (a launcher preset included) ends the panel
        if(s==screen::code) { code.close(); fixture_ticket=0; }
        pending.clear();keyboard_input.clear();error_visible=false;accepted=-1;
        if(!abandon_name()) { show_notice(menu);return; }
        // Original manager request deliberately bypasses our Online interception:
        // Other ways opens the full native name/profile/Host/Join chain.
        at<request_fn>(0x5d5db0)(manager(),destination,nullptr);
        return; // never bind after requesting a transition
    }
    const auto name=display(local_name,31);
    // The hub's large name field is 140 view units wide (title size); the other screens' header name 64 (row size).
    bind_text(menu,s==screen::hub ? "local_name" : "self_name",(s==screen::hub ? fit_name(name,140*.8,1.0) : fit_name(name,64*.8,.69)).c_str());
    {   // The header avatar's initial: the screen name's first allowlisted character, upper case ("?" never shown).
        char initial[2]{name.empty() || name[0]=='?' ? 'P' : char(std::toupper(static_cast<unsigned char>(name[0]))),0};
        bind_text(menu,"self_initial",initial);
    }
    if(recent) {
        bind_text(menu,"last_name",fit_name(display(recent->name,31),180*.8,.69).c_str());
        // "Code K7QN-2X9M, 2 hours ago": the code in its display form and the age of the stored record (never invented).
        const auto age=relative_age(wall_seconds(),recent->time);
        bind_text(menu,"last_code",("Code "+recent->code.substr(0,4)+"-"+recent->code.substr(4)+(age.empty() ? "" : ", "+age)).c_str());
        bind_text(menu,"last_time",age.c_str());
    }
    for(const auto* item:{"last_name","last_code","last_time"}) hide(menu,item,!recent);
    hide(menu,"lc_recent_empty",bool(recent));
    if(s==screen::code) {
        // Join by code's Paste / Clear prompts follow the device (060q live: they stayed "Ctrl+V / Y Paste", "Shift+Del / X Clear").
        // The engine rewrites the five description slots on every focus change, so the cached bindings are dropped then.
        static void* prompt_focus=nullptr;
        if(void* const focus=focus_of(menu); focus!=prompt_focus) { prompt_focus=focus;bindings.text.erase("desctext3");bindings.text.erase("desctext4"); }
        const bool pad=pad_probe && pad_probe(menu_player);
        bind_text(menu,"desctext3",pad ? "Y Paste" : "Ctrl+V Paste");
        bind_text(menu,"desctext4",pad ? "X Clear" : "Shift+Del Clear");
    }
    if(s==screen::hub && removed_notice) {
        // The kicked guest's Hub: a plain notice (lc_notice_removed), no host-left panel; the Hub stays fully usable.
        removed_notice=false;host_left_state.dismiss();message("lc_notice_removed");
    }
    if(s==screen::hub) {
        bool panel=host_left_state.active;
        // The 0.6 host-left panel (07-host-left, with only the section 5 body/button variants): modal, Host again / Back to Online.
        const bool veil=hide(menu,"lc_hub_host_left_veil",!panel),card=hide(menu,"lc_hub_host_left",!panel),
            again=hide(menu,"lc_hostleft_again",!panel),back=hide(menu,"lc_hostleft_back",!panel);
        if(panel && !(veil && card && again && back)) {
            // Assets without the panel (an older LC): never disable the rows behind a panel nobody can see.
            logger::write_once("online-host-left-assets","online menus: the host-left panel is not authored in these assets; the Hub stays usable");
            host_left_state.dismiss();panel=false;
            for(const auto* node:{"lc_hub_host_left_veil","lc_hub_host_left","lc_hostleft_again","lc_hostleft_back"}) hide(menu,node,true);
        }
        enable(menu,"rejoin",bool(recent) && !panel);
        // The rows (and the prompt items that repeat them: desctext3 Change name, desctext4 Rejoin) cannot be activated or
        // focused behind the veil.
        for(const auto* row:{"host","code","browse","other","change_name","desctext3","desctext4"}) enable(menu,row,!panel);
        for(const auto* button:{"lc_hostleft_again","lc_hostleft_back"}) enable(menu,button,panel);
        // Focus follows the modal: onto Host again when it opens, back onto the Hub's Host row when it closes.
        if(panel!=hub_panel_shown) { hub_panel_shown=panel;lobby_focus(menu,panel ? "lc_hostleft_again" : "host"); }
        hub_focus_follow(menu);
    }
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
    bind_broker(menu,s);
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
    if(party_owner==reinterpret_cast<std::uintptr_t>(menu)) { party_ui.reset();party_owner=0;party_origin.clear();party_feedback=0; }
    const auto s=which(menu);
    // +48 is resource cleanup and may run while constructing a noncurrent
    // cached menu. Only real +1c deactivation owns a requested Hub departure.
    const bool active_departure=departure && current()==menu;
    const bool native_departure=active_departure && native_flow_departure(s);
    if(s==screen::code && current()==menu) { code.close(); fixture_ticket=0; }
    if(active_departure && !native_departure && broker_live() && (s==screen::code || s==screen::browse || s==screen::host) && broker_view().state!=broker::phase::room) broker_cancel();
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
    if(s==screen::party) {
        if(!alt && control && edges[0x2e]) pending.post(int(intent::party_invite),address,s);
        if(!alt && control) state[0x2e]=0;
        return;
    }
    if(lobby(s)) {
        lobby_state.copy_keyboard=!alt && control && (state[0x2e]&0x80);
        if(!alt && control && edges[0x2e]) pending.post(int(intent::copy_code),address,s);
        if(!alt && control) state[0x2e]=0;
        return;
    }
    if(s==screen::browse && !alt && !control) {
        if(edges[0x3f]) pending.post(int(intent::refresh),address,s); // F5
        else if(edges[0xc9]) pending.post(int(intent::previous),address,s);
        else if(edges[0xd1]) pending.post(int(intent::next),address,s);
        state[0x3f]=state[0xc9]=state[0xd1]=0;return;
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
unsigned session_clear_count() { return session_clears; }
void set_pad_probe(bool (*probe)(int player)) { pad_probe=probe; }
void set_pointer_probe(bool (*probe)(int& x,int& y,int& screen_width,int& screen_height)) { pointer_probe=probe; }
bool pad_prompt_scope() {
    if(!ready) return false;
    auto* menu=current_peek();
    // The custom screens, and the skinned lobbies (fix E: the same pad scheme; an unskinned native lobby keeps its labels).
    return menu && (custom(which(menu)) || lobby_live(menu));
}
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
    c.writes.push_back(patch_transaction::relocation(0x69e49c,"",false,
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&menu_mouse)),0,"scoped base mouse hit boxes"));
    c.writes.push_back(patch_transaction::relocation(0x5bc3d0,"e8",true,
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&queue_stub)),0,"authored usecmd intent boundary"));
    const std::pair<unsigned,const void*> lobby_hooks[]{
        {0x69f284,reinterpret_cast<const void*>(&host_activate)},
        {0x69f34c,reinterpret_cast<const void*>(&join_activate)},
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
    c.writes.push_back(patch_transaction::relocation(0x5f1bb0,"e9",true,
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&setuphost_command)),0,"all native setuphost producers"));
    c.writes.push_back(patch_transaction::relocation(0x49faac,"e8",true,
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&setuphost_script_queue_stub)),0,"native setupHost script command generation"));
    for(const auto site:{0x6128abu,0x612d1fu,0x612ddau}) c.writes.push_back(patch_transaction::relocation(site,"e8",true,
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&native_session_cleared_stub)),0,"native terminal session clear ends room"));
    c.writes.push_back(patch_transaction::relocation(0x5b9609,"e8",true,
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&setuphost_queue_stub)),0,"native Host posting boundary"));
    c.writes.push_back({0x5b960e,{0x90,0x90},"native Post call padding"});
    c.writes.push_back(patch_transaction::relocation(0x5f1f8b,"e8",true,
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&console_open_stub)),0,"native console openmenu request ownership"));
    c.writes.push_back(patch_transaction::relocation(0x60a095,"e8",true,
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&native_host_queue_stub)),0,"native Host queued request ownership"));
    c.writes.push_back(patch_transaction::relocation(0x6a0348,"",false,
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&model_item_load)),0,"LC model scenes authored hidden"));
    c.writes.push_back(patch_transaction::relocation(0x69f66c,"",false,
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&options_activate)),0,"native Host settings activation"));
    c.writes.push_back(patch_transaction::relocation(0x69f694,"",false,
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&options_tick)),0,"native Host settings seed"));
    c.writes.push_back(patch_transaction::relocation(0x69f688,"",false,
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&options_back)),0,"native Host setup cancel"));
    return c;
}
void install(HMODULE game,const install_hooks* hooks) {
    if(ready || !requested()) return;
    // The co-op screens are authored for the private Barlow family only (live 060p f03: with
    // LegendsClassicFonts=0 the hub opened in the classic font with overlapping rows). Without the
    // committed font capability, Play Online stays the game's own menu.
    if(!lc_fonts::active()) {
        logger::write("online menus: [Game] LegendsClassicFonts is not active (off or refused); native online entry retained"); return;
    }
    if(reinterpret_cast<std::uintptr_t>(game)!=0x400000 ||
        !registration_fits(script_function_overflow::table(),script_functions::capacity())) {
        logger::write("online menus: registration/capacity refused; native online entry retained"); return;
    }
    if(hooks) {
        offline_fixture=hooks->broker==nullptr;
        if(hooks->broker) broker_calls=*hooks->broker;
        if(hooks->clock) clock_source=hooks->clock;
        if(hooks->wall_clock) wall_source=hooks->wall_clock;
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
    logger::write("online menus: dispatcher lcOnlineIntent n(i) registered; 42 hooks committed atomically; effective script capacity=%d; requires lc-online-menus/1 assets; input API=%s; lobby API=%s; broker API=%s; native API=%s; party API=%s; errors API=%s",script_functions::capacity(),input_capability,lobby_capability,broker_capability,native_capability,party_capability,errors_capability);
}
}

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
bool ready=false, entry_queued=false;
int accepted=-1;
DWORD accepted_at=0;
mailbox pending;
char local_name[32]{};
bool error_visible=false;

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
bool assets_ready() {
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

// EBX is the native MAIN_MENU input mask at the guarded call site; p is the
// controller returned with that input. Forward the native setter in all cases.
void __cdecl capture(void* m,int p,unsigned mask) {
    at<select_fn>(0x5d8560)(m,p);
    accepted=controller_valid(p) && (mask&0x10) && which(current())==screen::main ? p : -1;
    accepted_at=GetTickCount();
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

void __fastcall request(void* m,void*,const char* name,const char* args) {
    pending.clear();
    if(ready && name && std::strcmp(name,"online")==0 && which(current())==screen::main) {
        if(entry_queued) return;
        // Launcher presets cannot invent a controller; leave main available for
        // a real Play Online accept. No initializer or room work occurs here.
        if(!controller_valid(accepted) || GetTickCount()-accepted_at>1000) {
            logger::write("online menus: waiting for a real Play Online accept (controller 0..3)");
            return;
        }
        const int p=accepted; accepted=-1;
        native_engine engine;
        if(assets_ready() && initialize(engine,p)) {
            entry_queued=true; error_visible=false;
            logger::write("online menus: entry initialized once, controller=%d; Hub requested; no broker operation",p);
            at<request_fn>(0x5d5db0)(m,"lc_online",nullptr);
            return;
        }
        logger::write("online menus: asset/native initialization refused; keeping native online entry");
    }
    entry_queued=false;
    at<request_fn>(0x5d5db0)(m,name,args);
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
    static constexpr const char* commands[]{
        "runscript lcOnlineIntent(2)", "runscript lcOnlineIntent(3)",
        "runscript lcOnlineIntent(4)", "runscript lcOnlineIntent(5)",
        "runscript lcOnlineIntent(6)", "runscript lcOnlineIntent(7)",
        "runscript lcOnlineIntent(8)"
    };
    if(ready && line) for(unsigned i=0;i<std::size(commands);++i) {
        if(std::strcmp(line,commands[i])!=0) continue;
        auto* menu=current();
        if(live(menu)) pending.post(static_cast<int>(i)+2,reinterpret_cast<std::uintptr_t>(menu),which(menu));
        return true; // consumed once; never leave a delayed command behind
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

void bind_text(void* menu,const char* item,const char* text) {
    __try {
        auto* found=at<void*(__thiscall*)(void*,const char*)>(0x5adc10)(menu,item);
        if(!found) return;
        const auto* vt=*static_cast<const std::uint32_t**>(found);
        if(vt[0x78/4]!=0x5c6850 || *reinterpret_cast<const std::uint32_t*>(static_cast<char*>(found)+0xa0)) return;
        at<set_text_fn>(0x5adcf0)(menu,item,"%s",text);
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { }
}
void hide(void* menu,const char* item,bool hidden) {
    __try {
        auto* found=at<void*(__thiscall*)(void*,const char*)>(0x5adc10)(menu,item);
        if(found && (*static_cast<const std::uint32_t**>(found))[8/4]==0x5bcdb0)
            at<hide_fn>(0x5ade10)(menu,item,hidden);
    }
    __except(EXCEPTION_EXECUTE_HANDLER) { }
}
void __fastcall tick(void* menu,void*) {
    at<tick_fn>(0x5aaeb0)(menu);
    if(!ready || !live(menu)) { pending.clear(); return; }
    entry_queued=false;
    const auto s=which(menu);
    const auto action=static_cast<intent>(pending.take(reinterpret_cast<std::uintptr_t>(menu),s));
    const char* destination=nullptr;
    switch(action) {
    case intent::host: if(s==screen::hub) destination="lc_host"; break;
    case intent::code: if(s==screen::hub) destination="lc_code"; break;
    case intent::browse: if(s==screen::hub) destination="lc_browse"; break;
    case intent::other: if(s==screen::hub) destination="online"; break;
    case intent::back: destination=s==screen::hub ? "main" : "lc_online"; break;
    case intent::unavailable: error_visible=true; break;
    case intent::dismiss: error_visible=false; break;
    default: break;
    }
    if(destination) {
        pending.clear(); error_visible=false; accepted=-1;
        // Original manager request deliberately bypasses our Online interception:
        // Other ways opens the full native name/profile/Host/Join chain.
        at<request_fn>(0x5d5db0)(manager(),destination,nullptr);
        return; // never bind after requesting a transition
    }
    const auto name=display(local_name,31);
    bind_text(menu,s==screen::hub ? "local_name" : "self_name",name.c_str());
    bind_text(menu,"service","Offline preview");
    bind_text(menu,"self_state","Preview");
    bind_text(menu,"last_name","No recent game");
    bind_text(menu,"last_code","");
    bind_text(menu,"browse_hint","Offline preview");
    bind_text(menu,"lc_inline_error","This action is not available in this preview. Use Other ways to play.");
    hide(menu,"lc_inline_error",!error_visible); // true HIDES
}
std::uint32_t __fastcall close(void* menu,void*) {
    pending.clear(); entry_queued=false;
    return at<close_fn>(0x5af5a0)(menu);
}
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
    c.writes.push_back(patch_transaction::relocation(0x5bc3d0,"e8",true,
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&queue_stub)),0,"authored usecmd intent boundary"));
    return c;
}
void install(HMODULE game) {
    if(ready || !requested()) return;
    if(reinterpret_cast<std::uintptr_t>(game)!=0x400000 ||
        !registration_fits(script_function_overflow::table(),script_functions::capacity())) {
        logger::write("online menus: registration/capacity refused; native online entry retained"); return;
    }
    patch_transaction::transaction tx;
    tx.add(contribution());
    const auto result=tx.commit();
    if(result==patch_transaction::outcome::rollback_failed) game_version::refuse_startup();
    if(result!=patch_transaction::outcome::committed) {
        logger::write("online menus: %s; native online entry retained",tx.report().c_str()); return;
    }
    // Installation runs before game registration on a single thread; capacity
    // was reserved above. Publish the function only after every hook readback.
    const auto added=script_function_overflow::table().add(&registration);
    if(added!=script_functions_rules::added::added) {
        game_version::refuse_startup(); return;
    }
    ready=true;
    logger::write("online menus: dispatcher lcOnlineIntent n(i) registered; 5 hooks committed atomically; effective script capacity=%d; requires lc-online-menus/1 assets",script_functions::capacity());
}
}

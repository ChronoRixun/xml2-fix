// Production bridge with synthetic native services; maps the owned PE, never launches it.
#include "../src/join_in_progress.cpp"
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <cstdarg>

std::vector<std::string> observed_jip_logs;

namespace logger {
void write(const char* format,...) {
    char text[2048]{};va_list args;va_start(args,format);
    std::vsnprintf(text,sizeof text,format,args);va_end(args);
    observed_jip_logs.emplace_back(text);
}
void write_capped(std::string_view,const char* format,...) {
    // Capture the capped API too; log_test exercises its production rate gate.
    char text[2048]{};va_list args;va_start(args,format);
    std::vsnprintf(text,sizeof text,format,args);va_end(args);
    observed_jip_logs.emplace_back(text);
}
void write_once(std::string_view,const char*,...) {}
}
namespace e4_observe {
int native_exception(const char*,EXCEPTION_POINTERS*) { return EXCEPTION_CONTINUE_SEARCH; }
[[noreturn]] void fatal(const char*) { std::abort(); }
}
namespace save_format {
const save_format_rules::native_profile& installed_profile() { static save_format_rules::native_profile p{};return p; }
bool serialize_wrapper(std::uint8_t*) { assert(false);return false; }
}
namespace game_version { void refuse_startup() { assert(false); } }
namespace gameplay_agreement {
bool register_rule(std::uint16_t,std::uint16_t,std::array<std::uint32_t,4>,bool) { assert(false);return false; }
}
namespace gns_transport { bool requested() { return false; } bool installed() { return false; } }
namespace xml1_turrets { std::uint32_t fixture_hook = 0; std::uint32_t installed_process_hook() { return fixture_hook; } }
namespace entity_capacity { std::uint32_t installed_target(std::uint32_t) { return 0; } const entity_capacity_layout::descriptor& installed() { static const entity_capacity_layout::descriptor stock; return stock; } }
namespace entity_transfer { void observe_prepare(void(__cdecl*)(const void*)) { assert(false); } }
namespace ini {
int number(const wchar_t*,const wchar_t*,int n) { return n; }
bool flag(const wchar_t*,const wchar_t*,bool n) { return n; }
}
namespace {
unsigned adds=0,barriers=0,resumes=0;
alignas(8) unsigned char s[0x500]{},m[0x2400]{},pause[0xb0]{},local[0xc0]{},remote[0xc0]{},existing[0xc0]{},nodes[3][0x20]{};
unsigned controller_vt[32]{},seat_vt[32]{},player_vt[32]{};
unsigned* controllers=controller_vt;unsigned* seats=seat_vt;unsigned* players=player_vt;
template<class T> void put(void* p,unsigned offset,T value) { std::memcpy(static_cast<char*>(p)+offset,&value,sizeof value); }
void redirect(unsigned va,const void* to) {
    auto* p=reinterpret_cast<unsigned char*>(va);p[0]=0xe9;put(p,1,std::int32_t(reinterpret_cast<std::uintptr_t>(to)-va-5));
}
void* __cdecl session() { return s; }
void* __cdecl manager() { return m; }
void* __cdecl zones() { return nullptr; }
void* __cdecl pause_get() { return pause; }
void __fastcall resume(void*,void*) { ++resumes; }
// Enter the retail scheduler after its task walks; preserve its saved ESI ABI.
__declspec(naked) void scheduler_resume() {
    __asm {
        push esi
        mov eax,60aac5h
        jmp eax
    }
}
void* __cdecl controllers_get() { return &controllers; }
void* __cdecl seats_get() { return &seats; }
void* __cdecl players_get() { return &players; }
int __fastcall assign(void*,void*,int index,char) { return index; }
void __fastcall noop_one(void*,void*,int) {}
void __fastcall noop(void*,void*) {}
char __fastcall player_add(void*,void*,void*,int) { return 1; }
void __fastcall add(void*,void*,void* record) { assert(join_in_progress::area_load && record==remote);++adds; }
void __fastcall barrier(void*,void*,float) { ++barriers; }
#include "join_in_progress_gamespy.inl"
#include "join_in_progress_checksum.inl"
#include "join_in_progress_restart.inl"
#include "join_in_progress_refusal_transport.inl"
}
int main(int argc,char** argv) {
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* error)->LONG {
        std::cerr<<"JIP fixture fault eip="<<std::hex<<error->ContextRecord->Eip<<" code="<<error->ExceptionRecord->ExceptionCode<<" address="<<error->ExceptionRecord->ExceptionInformation[1]<<"\n";
        ExitProcess(error->ExceptionRecord->ExceptionCode);
    });
    assert(argc==2 || argc==3);
    std::ifstream f(argv[1],std::ios::binary);std::vector<unsigned char> image((std::istreambuf_iterator<char>(f)),{});
    assert(image.size()>0x1000);
    auto word=[&](unsigned at) { unsigned short n;std::memcpy(&n,image.data()+at,2);return n; };
    auto dword=[&](unsigned at) { unsigned n;std::memcpy(&n,image.data()+at,4);return n; };
    DWORD old;assert(VirtualProtect(reinterpret_cast<void*>(0x401000),0x700000,PAGE_EXECUTE_READWRITE,&old));
    const auto pe=dword(0x3c),sections=pe+24+word(pe+20);
    for(unsigned i=0;i<word(pe+6);++i) { const auto at=sections+40*i;std::memcpy(reinterpret_cast<void*>(0x400000+dword(at+12)),image.data()+dword(at+20),dword(at+16)); }
    using namespace join_in_progress_rules;
    if(argc==3) {
        if(std::string_view(argv[2])=="refusal-transport") { refusal_transport_fixture::run();return 0; }
        if(std::string_view(argv[2])=="checksum") { checksum_fixture::run();return 0; }
        if(std::string_view(argv[2])=="send-set") { checksum_fixture::send_set_run();return 0; }
        if(std::string_view(argv[2])=="admission") { checksum_fixture::admission_run();return 0; }
        if(std::string_view(argv[2])=="restart") { restart_fixture::run();return 0; }
        if(std::string_view(argv[2])=="turrets") { restart_fixture::run_turrets();return 0; }
        assert(std::string_view(argv[2])=="gamespy" || std::string_view(argv[2])=="gamespy-stock");
        gamespy_fixture::run(std::string_view(argv[2])=="gamespy");return 0;
    }
    redirect(session_getter,session);redirect(manager_getter,manager);redirect(zones_getter,zones);
    redirect(add_entry,add);redirect(area_barrier,barrier);
    redirect(controllers_getter,controllers_get);redirect(seats_getter,seats_get);redirect(players_getter,players_get);
    redirect(record_controller,noop_one);
    controller_vt[controller_assign_slot/4]=unsigned(reinterpret_cast<std::uintptr_t>(assign));
    seat_vt[seats_activate_slot/4]=unsigned(reinterpret_cast<std::uintptr_t>(noop_one));
    seat_vt[seats_update_slot/4]=unsigned(reinterpret_cast<std::uintptr_t>(noop));
    player_vt[player_added_slot/4]=unsigned(reinterpret_cast<std::uintptr_t>(player_add));
    put(local,record_flags,1u);put(local,0x18,0);put(remote,0x18,1);
    put(s,0x26c,nodes[0]);put(s,0x278,2u);put(nodes[0],node_record,local);put(nodes[0],node_next,nodes[1]);put(nodes[1],node_record,remote);
    auto& machine=join_in_progress::machine;machine.started=true;
    assert(machine.defer_add(remote,1,false,0));
    // Execute retail 0x60c350: the current host loaded, the pending guest cannot
    // report loaded yet. Team must open without activating or waiting for it.
    put(pause,pause_loaded,1u);
    const auto native_query=reinterpret_cast<char(__thiscall*)(void*)>(all_loaded);
    assert(native_query(pause)==0);
    for(unsigned i=0;i<100;++i) {
        assert(join_in_progress::all_loaded_bridge(pause,nullptr)==1);
        assert(machine.pending(remote) && !machine.holding() && adds==0);
    }
    assert(native_query(pause)==0 && *reinterpret_cast<unsigned*>(pause+pause_loaded+4)==0); // no fabricated grid write
    put(pause,pause_loaded,0u);assert(join_in_progress::all_loaded_bridge(pause,nullptr)==0);
    put(pause,pause_loaded,1u);
    // A third, existing participant still holds the real loaded barrier.
    put(existing,0x18,2);put(nodes[1],node_next,nodes[2]);put(nodes[2],node_record,existing);put(s,0x278,3u);
    assert(join_in_progress::all_loaded_bridge(pause,nullptr)==0);
    put(pause,pause_loaded+8,1u);assert(join_in_progress::all_loaded_bridge(pause,nullptr)==1);
    put(nodes[1],node_next,static_cast<void*>(nullptr));put(s,0x278,2u);
    // Closing Team uses the independent not-ready flag gate, even after loaded succeeds.
    put(local,record_flags,1u);put(remote,record_flags,0x10u);
    assert(reinterpret_cast<char(__thiscall*)(void*)>(0x60c200)(pause)==1);
    assert(join_in_progress::ready_bridge(pause,nullptr)==0);
    put(local,record_flags,0x11u);assert(join_in_progress::ready_bridge(pause,nullptr)==1);
    put(local,record_flags,1u);
    // Execute the actual host scheduler resume chain: original call stays paused,
    // the production replacement resumes, and an unready current hero still blocks.
    put(s,0x421,static_cast<unsigned char>(1));put(pause,0x6c,static_cast<unsigned char>(1));
    redirect(pause_getter,pause_get);redirect(0x60c390,resume);
    scheduler_resume();assert(resumes==0);
    const auto patched=join_in_progress::rel(0xe8,ready_calls[2],reinterpret_cast<const void*>(&join_in_progress::ready_bridge),5);
    std::memcpy(reinterpret_cast<void*>(ready_calls[2]),patched.data(),patched.size());
    scheduler_resume();assert(resumes==1 && machine.pending(remote) && adds==0);
    put(local,record_flags,0x11u);scheduler_resume();assert(resumes==1);put(local,record_flags,1u);
    // Existing loaded spectators in a solo zone have no hero Ready action.
    put(nodes[1],node_next,nodes[2]);put(s,0x278,3u);put(existing,record_flags,0x10u);
    auto* game=reinterpret_cast<void*>(game_static);auto* zone=reinterpret_cast<void*>(zones_static);
    put(game,0,game_vtable);put(zone,0,zones_vtable);
    std::memcpy(static_cast<char*>(zone)+zones_name_at,"synthetic/solo",15);
    put(zone,zones_loading_at,static_cast<unsigned char>(0));
    put(game,game_party,1u);put(game,game_slot_human,static_cast<unsigned char>(1));put(game,game_slot_controller,0);
    put(pause,0x6c,static_cast<unsigned char>(0));join_in_progress::spectate();
    put(pause,0x6c,static_cast<unsigned char>(1));
    assert(join_in_progress::ready_bridge(pause,nullptr)==0); // hero 0 ready; watcher 2 cannot Ready
    scheduler_resume();assert(resumes==2 && machine.pending(remote));
    put(game,game_slot_controller,2);scheduler_resume();assert(resumes==2);assert(join_in_progress::ready_bridge(pause,nullptr)==1); // player 2 now has a hero
    put(game,game_slot_controller,0);
    // A player who HAD a hero outside Team must keep the Ready barrier while
    // their seat is temporarily empty in its picker. Use the actual retail
    // scheduler chain, not only the bridge return value.
    put(pause,0x6c,static_cast<unsigned char>(0));
    put(game,game_party+4,2u);put(game,game_slot_human+1,static_cast<unsigned char>(1));put(game,game_slot_controller+4,2);
    join_in_progress::spectate();
    put(pause,0x6c,static_cast<unsigned char>(1));put(game,game_party+4,0u);
    scheduler_resume();assert(resumes==2);assert(join_in_progress::ready_bridge(pause,nullptr)==1);
    put(pause,pause_loaded+8,0u);
    assert(join_in_progress::ready_bridge(pause,nullptr)==1); // an unloaded peer is never a spectator exception
    put(pause,pause_loaded+8,1u);join_in_progress::area_load=true;
    assert(join_in_progress::ready_bridge(pause,nullptr)==1); // zone-boundary barrier stays native
    join_in_progress::area_load=false;join_in_progress::epoch.active=true;
    assert(join_in_progress::ready_bridge(pause,nullptr)==1);join_in_progress::epoch={};
    machine.started=false;assert(join_in_progress::ready_bridge(pause,nullptr)==1);machine.started=true;
    put(nodes[1],node_next,static_cast<void*>(nullptr));put(s,0x278,2u);
    // A real synchronous area boundary still activates exactly once.
    put(pause,0x6c,static_cast<unsigned char>(0));
    join_in_progress::area_barrier_bridge(s,nullptr,5.f);
    assert(adds==1 && barriers==1 && !machine.pending(remote) && machine.holding());
    join_in_progress::area_barrier_bridge(s,nullptr,5.f);assert(adds==1 && barriers==2);
    std::cout<<"PASS production JIP bridge + retail loaded query: 100 loaded queries; ready close excludes pending and loaded solo watchers, active heroes still wait; retail scheduler resumes pending/solo cohorts; native area/restart/startup gates; boundary adds once\n";
}

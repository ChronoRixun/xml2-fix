// Production bridge with synthetic native services; maps the owned PE, never launches it.
#include "../src/join_in_progress.cpp"
#include "fake_d3d8.hpp" // p060fix/AQ2: the hold overlay on a CPU fake device (join_in_progress_hold.inl)
#include "actor_pools_plan.hpp" // p060fix/AI: the production W11 relocation plan, installed first in the `actorpools` mode
#include "entity_world.hpp"
#include "entity_grid.hpp"
#include "entity_query.hpp"
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <cstdarg>

std::vector<std::string> observed_jip_logs;
namespace frame_rate {
unsigned fixture_shared_starts=0;
void shared_frame_started() { ++fixture_shared_starts; }
unsigned session_rate() { return 0; } // AR7 belt: no adopted session rate in the fixture (the stock 30 applies)
}

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
namespace gns_transport {
bool requested() { return false; } bool installed() { return false; }
unsigned fixture_boundary_announcements=0,fixture_boundary_services=0;
std::uint16_t fixture_boundary_frame=0;
std::uint8_t fixture_announced_mask=0;
std::uint8_t fixture_boundary_mask=4;
bool fixture_boundary_ready=true;
void (*fixture_boundary_service)()=nullptr;
void announce_join_boundary(std::uint16_t frame,std::uint8_t mask) { fixture_boundary_frame=frame;fixture_announced_mask=mask;++fixture_boundary_announcements; }
bool poll_join_boundary(std::uint16_t,std::uint8_t& mask) { mask=fixture_boundary_mask;return fixture_boundary_ready; }
void service_join_boundary() { ++fixture_boundary_services;if(fixture_boundary_service) fixture_boundary_service(); }
void clear_join_boundaries() {}
// p060fix/AR: the transport half of a lost newcomer (its decisions and the restart entry) is recorded for the fixture.
unsigned fixture_restart_entries=0,fixture_session_entries=0,fixture_carry_polls=0;std::uint8_t fixture_admitted=0;int fixture_carry_refusals=0;
void note_admission_newcomers(std::uint8_t mask) { fixture_admitted|=mask; }
void newcomers_restart_entry(const char*,bool session_ended) { ++(session_ended ? fixture_session_entries : fixture_restart_entries); fixture_admitted=0; }
bool newcomer_decisions_carried(const char*) { ++fixture_carry_polls; if(fixture_carry_refusals>0) { --fixture_carry_refusals;return false; } return fixture_carry_refusals==0; }
}
namespace xml1_turrets { std::uint32_t fixture_hook = 0; unsigned fixture_epochs = 0; std::uint32_t installed_process_hook() { return fixture_hook; } void cohort_epoch(float) { ++fixture_epochs; } digest probe_digest(unsigned) { return {}; } }
namespace actor_pools { std::vector<std::uint8_t> fixture_controller_reset; const std::vector<std::uint8_t>& installed_controller_reset() { return fixture_controller_reset; } }
namespace e4_core { bool fixture_installed = false; bool installed() { return fixture_installed; } }
namespace pad_prompts { unsigned device_switches() { return 0; } }
namespace entity_capacity { entity_capacity_layout::descriptor fixture_descriptor; std::uint32_t installed_target(std::uint32_t) { return 0; } const entity_capacity_layout::descriptor& installed() { return fixture_descriptor; } }
namespace entity_transfer { void observe_prepare(void(__cdecl*)(const void*)) { assert(false); } }
namespace display { void (*fixture_overlay)(void*) = nullptr; void set_overlay(void (*draw)(void*)) { fixture_overlay = draw; } } // p060fix/AQ2: the hold's message hook
// int31: the online menus' 0x6141e0 accessor (the AS guard composes with exactly a JMP to it); a synthetic getter here.
namespace online_menus { const char* __fastcall fixture_display_name(void* p,void*) { return reinterpret_cast<const char*>(p)+0x40; }
const void* display_name_getter() { return reinterpret_cast<const void*>(&fixture_display_name); } }
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
#include "join_in_progress_execution_fixture.inl"
#include "join_in_progress_restart.inl"
#include "join_in_progress_refusal_transport.inl"
// int31: AS + online menus - the 0x6141e0 accessor guard accepts retail bytes or exactly `e9 rel32` to
// online_menus::display_name_getter(); a JMP elsewhere, another opcode or any other altered AS byte still refuses.
void as_menus_composition() {
    auto* site=reinterpret_cast<unsigned char*>(0x6141e0);
    unsigned char retail[5];std::memcpy(retail,site,5);
    assert(join_in_progress::mismatch()==nullptr && !join_in_progress::composed_display_name);
    auto jmp=[&](std::uintptr_t target) { site[0]=0xe9;const auto rel=static_cast<std::uint32_t>(target-0x6141e0-5);std::memcpy(site+1,&rel,4); };
    jmp(reinterpret_cast<std::uintptr_t>(online_menus::display_name_getter()));
    assert(join_in_progress::mismatch()==nullptr && join_in_progress::composed_display_name);
    jmp(reinterpret_cast<std::uintptr_t>(online_menus::display_name_getter())+1);
    { const auto* g=join_in_progress::mismatch();assert(g && g->va==0x6141e0); }
    jmp(reinterpret_cast<std::uintptr_t>(online_menus::display_name_getter()));site[0]=0xe8;
    { const auto* g=join_in_progress::mismatch();assert(g && g->va==0x6141e0); }
    jmp(reinterpret_cast<std::uintptr_t>(online_menus::display_name_getter()));
    auto* other=reinterpret_cast<unsigned char*>(0x6110c0);const auto keep=*other;*other^=0xff;
    assert(join_in_progress::mismatch()!=nullptr);
    *other=keep;assert(join_in_progress::mismatch()==nullptr);
    std::memcpy(site,retail,5);
    assert(join_in_progress::mismatch()==nullptr && !join_in_progress::composed_display_name);
    std::cout<<"PASS AS/online menus: the 0x6141e0 accessor guard takes retail bytes or exactly the menus' JMP to display_name_getter; a wrong target, opcode or other AS byte refuses\n";
}
// b2d-fix-jip: the restart-release barrier through the production store hook (input_epoch_store_filter) against the retail
// store 0x559560 of the mapped image (unhooked here: a filter result 0 means the fixture calls the retail store as the bridge
// would; the replay at the cap calls it itself). The blocks live at 0x79fbb0 inside the mapped .data.
void input_epoch_fixture() {
    using namespace join_in_progress;
    using namespace gns_transport_rules;
    auto* sync=reinterpret_cast<unsigned char*>(0x79fbb0);
    std::memset(sync,0,0xc700);
    auto block=[&](unsigned i) { return sync+i*input_block_size; };
    auto u32=[&](unsigned i,unsigned at) { std::uint32_t v;std::memcpy(&v,block(i)+at,4);return v; };
    auto u16=[&](unsigned i,unsigned at) { std::uint16_t v;std::memcpy(&v,block(i)+at,2);return v; };
    auto slot_label=[&](unsigned i,unsigned slot) { return u16(i,input_records+slot*input_record+4); };
    using store_t=void(__thiscall*)(void*,const void*,std::uint32_t,std::uint32_t);
    const auto store=reinterpret_cast<store_t>(join_in_progress_rules::input_epoch_store_site);
    // The decoder's shape: a single record takes the block counter as its label, then counter++ (0x559ee7..0x559f01); a batch
    // record carries its wire label. Then the store (through the hook: filter 0 = the native store runs).
    auto deliver=[&](unsigned index,std::uint32_t mask,std::uint32_t kind,bool batch=false,std::uint16_t wire_label=0) {
        alignas(4) unsigned char record[0x24]{};
        std::uint16_t label=wire_label;
        if(!batch) { auto* counter=reinterpret_cast<std::uint16_t*>(block(index)+join_in_progress_rules::input_block_counter);label=*counter;++*counter; }
        std::memcpy(record,&label,2);std::memcpy(record+4,&mask,4);
        std::uint32_t args[4]{0,static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(record)),index,kind};
        const int skip=input_epoch_store_filter(args);
        if(!skip) store(sync,record,index,kind);
        return skip;
    };
    auto logged=[&](const char* text) { for(const auto& l:observed_jip_logs) if(l.find(text)!=std::string::npos) return true;return false; };
    const std::uint32_t real=0x00080000u,marker=join_in_progress_core::input_epoch_mark_for(0x2d25);
    assert(join_in_progress::mismatch()==nullptr);
    // 1. RestartInputEpoch=0 / never armed: the stock path - a record is stored as it comes (write 1, next 1, label 0).
    assert(!input_epoch_hold.any());
    assert(deliver(2,real,0)==0 && u32(2,input_write)==1 && u16(2,input_next)==1 && slot_label(2,0)==0 && u16(2,join_in_progress_rules::input_block_counter)==1);
    std::memset(block(2),0,input_block_size);
    // 2. Armed at this PC's ring reset (index 0): P (index 2)'s records before its marker are held, not stored - a full
    //    record and a delta kind alike; the decoder's counter keeps counting (labels 0, 1 travel with the copies).
    observed_jip_logs.clear();
    input_epoch_hold.arm(0,GetTickCount());
    assert(deliver(2,real,0)==1 && deliver(2,0,2)==1);
    assert(u32(2,input_write)==0 && u16(2,input_next)==0 && u16(2,join_in_progress_rules::input_block_counter)==2 && input_epoch_hold.held[2]==2);
    { std::uint16_t l0,l1;std::memcpy(&l0,input_epoch_held[2][0].bytes,2);std::memcpy(&l1,input_epoch_held[2][1].bytes,2);assert(l0==0 && l1==1 && input_epoch_held[2][0].kind==0 && input_epoch_held[2][1].kind==2); }
    assert(logged("record(s) arrive before its epoch marker") && logged("held, not stored"));
    // 3. P's marker: the block resets (counter 0), the two held copies are dropped, the expectation ends; its second copy is a repeat.
    assert(deliver(2,marker,0)==1 && u16(2,join_in_progress_rules::input_block_counter)==0 && u32(2,input_write)==0 && !input_epoch_hold.expecting[2] && input_epoch_hold.held[2]==0);
    assert(logged("(2 held pre-marker record(s) dropped)") && logged("its block reset here (read 0 write 0 next 0 counter 3: 0 stale record(s) dropped)")); // counter 3: the decoder counted the marker too before the store
    assert(deliver(2,marker,0)==1 && u16(2,join_in_progress_rules::input_block_counter)==0);
    // 4. P's first real record after the marker is label 0 on this peer (as in the sender's ring).
    assert(deliver(2,real,0)==0 && u32(2,input_write)==1 && u16(2,input_next)==1 && slot_label(2,0)==0);
    assert(deliver(2,real,0)==0 && u32(2,input_write)==2 && u16(2,input_next)==2 && slot_label(2,1)==1);
    // 5. This PC's own records (the own-record store 0x55a2a6) are never held.
    assert(input_epoch_hold.expecting[1] && !input_epoch_hold.expecting[0]);
    assert(deliver(0,real,0)==0 && u32(0,input_write)==1);
    std::cout<<"PASS b2d barrier: P's pre-marker records are held (not stored), dropped at its marker with the block reset; its first real record is label 0; own records never held\n";
    // 6. The cap: 5 s after this PC's reset with no marker from index 3, its held records are stored in the order they
    //    came through the retail store (labels 0, 1), then the new one (2): the stock labelling. A marker afterwards, once a
    //    record was consumed (read 1), is late and ignored (b2c-fix-crash), never a reset.
    observed_jip_logs.clear();
    assert(deliver(3,real,0)==1 && deliver(3,real,2)==1 && input_epoch_hold.held[3]==2 && u32(3,input_write)==0);
    input_epoch_hold.armed_at-=join_in_progress_core::input_epoch_hold_cap_ms;
    assert(deliver(3,real,0)==0 && !input_epoch_hold.expecting[3] && input_epoch_hold.held[3]==0 && !input_epoch_replaying);
    assert(u32(3,input_write)==3 && u16(3,input_next)==3 && slot_label(3,0)==0 && slot_label(3,1)==1 && slot_label(3,2)==2 && u16(3,join_in_progress_rules::input_block_counter)==3);
    assert(logged("sent no epoch marker within the cap") && logged("its 2 held record(s) stored in the order they came"));
    { std::uint32_t one=1;std::memcpy(block(3)+input_read,&one,4); }
    assert(deliver(3,join_in_progress_core::input_epoch_mark_for(0x3aaa),0)==1 && u32(3,input_write)==3 && u16(3,input_next)==3 && u16(3,join_in_progress_rules::input_block_counter)==3 && logged("arrived late") && logged("IGNORED"));
    std::cout<<"PASS b2d barrier: at the cap the held records are stored through the retail store in order (stock labelling); a marker after consumption stays late/ignored\n";
    // 7. A full hold (96 records) flushes before the cap; the flush stores all 96 then the 97th (labels 0..96).
    observed_jip_logs.clear();
    std::memset(block(1),0,input_block_size);
    input_epoch_hold.arm(0,GetTickCount());
    for(unsigned i=0;i<join_in_progress_core::input_epoch_hold_records;++i) assert(deliver(1,real,0)==1);
    assert(input_epoch_hold.held[1]==join_in_progress_core::input_epoch_hold_records && u32(1,input_write)==0);
    assert(deliver(1,real,0)==0 && u32(1,input_write)==97 && u16(1,input_next)==97 && slot_label(1,96)==96 && logged("sent no epoch marker before the hold filled"));
    // 8. Holds are per participant: index 2 keeps holding while index 1 flushed; a replayed record passes the filter.
    assert(input_epoch_hold.expecting[2] && deliver(2,real,0)==1 && input_epoch_hold.held[2]==1);
    input_epoch_replaying=true;assert(deliver(2,real,0)==0 && input_epoch_hold.held[2]==1);input_epoch_replaying=false;
    // 9. The session's end / this PC's next ring reset disarms: nothing is held afterwards.
    input_epoch_hold.disarm();
    assert(!input_epoch_hold.any() && deliver(2,real,0)==0);
    assert(join_in_progress::mismatch()==nullptr);
    std::memset(sync,0,0xc700);
    std::cout<<"PASS b2d barrier: a full hold flushes in order; holds are per participant; replayed records pass; disarmed = stock\n";
}
void e4_guard_composition() {
    as_menus_composition();
    assert(join_in_progress::mismatch()==nullptr);
    // Exact E4 operand edits from its published 96-controller layout. The last
    // byte of the pop prefix also belongs to its mandatory observer detour.
    auto decode=[](const char* h) { std::vector<unsigned char> b;while(*h){char pair[3]{h[0],h[1],0};b.push_back(static_cast<unsigned char>(std::strtoul(pair,nullptr,16)));h+=2;}return b; };
    const auto init=decode("5633f65789b1104c020089b1144c020089b1184c020033d2");
    const auto pop=decode("8bd18b82144c02008b84828c4a020056");
    std::memcpy(reinterpret_cast<void*>(0x522f70),init.data(),init.size());
    std::memcpy(reinterpret_cast<void*>(0x522a10),pop.data(),pop.size());
    actor_ai::published={2,0x30000000,0x30000004,0x61c,96,0x30024db8,0x268,96,2,0x24db0,0x525302};
    actor_ai::observed_spans={{0x522a1f,{0x56},{0xe9,0x12,0x34,0x56,0x78}}};
    *reinterpret_cast<unsigned char*>(0x522a1f)=0xe9;
    assert(join_in_progress::mismatch()!=nullptr); // metadata alone is no successful E4 install
    e4_core::fixture_installed=true;
    assert(join_in_progress::mismatch()==nullptr); // failing-first: the old raw guard refuses E4
    for(const auto va:{0x522f70u,0x522a10u}) {
        const auto guard=std::find_if(join_in_progress_rules::guards.begin(),join_in_progress_rules::guards.end(),[&](const auto& g){return g.va==va;});
        assert(guard!=join_in_progress_rules::guards.end());
        for(unsigned i=0;i<guard->hex.size()/2;++i) {
            auto* byte=reinterpret_cast<unsigned char*>(va+i);*byte^=1;
            assert(join_in_progress::mismatch()==&*guard); // opcode, operand and observer bytes all remain guarded
            *byte^=1;
        }
    }
    actor_ai::observed_spans.clear();assert(join_in_progress::mismatch()->va==0x522a10);
    actor_ai::observed_spans={{0x522a1f,{0x56},{0xe9,0x12,0x34,0x56,0x78}}};
    actor_ai::published.controllers=32;assert(join_in_progress::mismatch()!=nullptr);
    actor_ai::published.controllers=96;actor_ai::published.stage=1;assert(join_in_progress::mismatch()!=nullptr);
    std::cout<<"PASS E4/JIP guards: certified stage2/96 operands plus reported observer accepted; uninstalled/wrong-layout/unreported and every altered prefix byte refused\n";
}
}
int main(int argc,char** argv) {
    SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* error)->LONG {
        std::cerr<<"JIP fixture fault eip="<<std::hex<<error->ContextRecord->Eip<<" code="<<error->ExceptionRecord->ExceptionCode<<" address="<<error->ExceptionRecord->ExceptionInformation[1]
            <<" eax="<<error->ContextRecord->Eax<<" ecx="<<error->ContextRecord->Ecx<<" edx="<<error->ContextRecord->Edx<<" esi="<<error->ContextRecord->Esi<<" esp="<<error->ContextRecord->Esp<<" ar5_stage="<<execution_fixture::fixture_stage<<"\n";
        const auto* stack=reinterpret_cast<const unsigned*>(error->ContextRecord->Esp);
        std::cerr<<"stack="<<stack[0]<<","<<stack[1]<<","<<stack[2]<<","<<stack[3]<<","<<stack[4]<<","<<stack[5]<<"\n";
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
        const std::string_view mode=argv[2];
        if(mode=="e4-guards" || mode=="actorpools" || mode=="turrets")
            join_in_progress::execution_observer=join_in_progress::execution_wrapper=true;
        if(std::string_view(argv[2])=="execution") { execution_fixture::run();return 0; }
        if(std::string_view(argv[2])=="input-epoch") { input_epoch_fixture();return 0; } // b2d-fix-jip
        if(std::string_view(argv[2])=="e4-guards") { e4_guard_composition();std::cout<<"PASS AR5 optional contracts compose with E4 guards\n";return 0; }
        if(std::string_view(argv[2])=="refusal-transport") { refusal_transport_fixture::run();return 0; }
        if(std::string_view(argv[2])=="checksum") { checksum_fixture::run();return 0; }
        if(std::string_view(argv[2])=="send-set") { checksum_fixture::send_set_run();return 0; }
        if(std::string_view(argv[2])=="admission") { checksum_fixture::admission_run();return 0; }
        if(std::string_view(argv[2])=="restart") { restart_fixture::run();return 0; }
        if(std::string_view(argv[2])=="turrets") { restart_fixture::run_turrets();std::cout<<"PASS AR5 optional contracts compose with turrets\n";return 0; }
        if(std::string_view(argv[2])=="actorpools") { restart_fixture::run_actor_pools();std::cout<<"PASS AR5 optional contracts compose with ActorPools\n";return 0; }
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

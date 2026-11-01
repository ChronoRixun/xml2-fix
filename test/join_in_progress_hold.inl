// p060fix/AQ + AQ2: the join hold against the mapped retail image (no launch). The world-section gate bridge is entered
// by CALL exactly as installed over `test byte ptr [esi+0x728], 2` at 0x46f0a9; a main-loop iteration is modelled as
// that gate plus, when the site's je would NOT skip, one world step (a synchronized draw and a game-time step).
// AQ2: the host's hold is armed through the PRODUCTION admission - area_barrier_bridge -> guarded_advance -> advance
// -> run_add (the native add's natives stubbed) - and opens through the production pump poll at the first pump after
// that zone load (the zone-load flag is a fixture switch); every iteration between the admission and the shared start
// is checked to run no world step, draw nothing and advance no game time. An existing participant's hold opens
// through the production campaign_notice of a late-restart 0x47. The production deferred_campaign_start releases it
// right before the native campaign start (stubbed: it records what the world looked like at that moment). The
// on-screen message is the production overlay (display::set_overlay's hook, hold_overlay.cpp) drawn on a CPU fake
// IDirect3DDevice8 (test/fake_d3d8.hpp), read back pixel by pixel.
alignas(16) unsigned char hold_game[0x800]{};
const void* hold_bridge_target=reinterpret_cast<const void*>(&join_in_progress::world_gate_bridge);
unsigned hold_flags=0,hold_regs[7]{};
__declspec(naked) void hold_probe() {
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        lea esi, hold_game
        mov eax, 11111111h
        mov ebx, 22222222h
        mov ecx, 33333333h
        mov edx, 44444444h
        mov edi, 55555555h
        mov ebp, 66666666h
        call dword ptr [hold_bridge_target]
        pushfd
        pop dword ptr [hold_flags]
        mov dword ptr [hold_regs], eax
        mov dword ptr [hold_regs+4], ebx
        mov dword ptr [hold_regs+8], ecx
        mov dword ptr [hold_regs+12], edx
        mov dword ptr [hold_regs+16], esi
        mov dword ptr [hold_regs+20], edi
        mov dword ptr [hold_regs+24], ebp
        pop ebp
        pop edi
        pop esi
        pop ebx
        ret
    }
}
// The bridge as the site sees it: ZF decides the je. Registers must come back exactly as they went in.
bool hold_gate_skips(unsigned char flag_byte) {
    hold_game[join_in_progress_rules::world_gate_flag_at]=flag_byte;
    hold_probe();
    assert(hold_regs[0]==0x11111111u && hold_regs[1]==0x22222222u && hold_regs[2]==0x33333333u && hold_regs[3]==0x44444444u);
    assert(hold_regs[4]==unsigned(reinterpret_cast<std::uintptr_t>(hold_game)) && hold_regs[5]==0x55555555u && hold_regs[6]==0x66666666u);
    const unsigned five=hold_flags&0x8c5u;
    assert(five==0x44u || five==0x00u); // exactly the flags retail's test leaves (zero result / bit set)
    return (hold_flags&0x40u)!=0;
}
unsigned hold_world_steps=0;
// One main-loop iteration's game update: the world section runs only when the gate does not skip.
bool hold_iteration() {
    if(hold_gate_skips(0x02)) return false; // bit 1 of game+0x728 set: retail would run the section
    ++hold_world_steps;
    ++*reinterpret_cast<unsigned*>(join_in_progress_rules::rng_count);
    *reinterpret_cast<unsigned*>(join_in_progress_rules::game_static+join_in_progress_rules::game_time_at)+=0x10;
    return true;
}
// Synthetic clock (0x55b610 getter, vt+0x28 = seconds) and scheduler (0x60b210 getter) for the deferred start's natives.
float hold_seconds=100.0f;
float __fastcall hold_clock_seconds(void*,void*,int) { return hold_seconds; }
unsigned hold_clock_vt[0x40]{};
struct { unsigned* vt; } hold_clock_object{hold_clock_vt};
void* __cdecl hold_clock_get() { return &hold_clock_object; }
alignas(16) unsigned char hold_scheduler[0x400]{};
void* __cdecl hold_scheduler_get() { return hold_scheduler; }
DWORD hold_ms=500000;
DWORD WINAPI hold_fake_ms() { return hold_ms; }
bool hold_loading=false;
bool hold_fake_loading() { return hold_loading; }
unsigned hold_begins=0,hold_adds=0;bool hold_at_begin=true;unsigned hold_draws_at_begin=0,hold_steps_at_begin=0,hold_time_at_begin=0;
void __cdecl hold_campaign_begin(int) {
    ++hold_begins;hold_at_begin=join_in_progress::world_hold!=0;hold_draws_at_begin=*reinterpret_cast<unsigned*>(join_in_progress_rules::rng_count);hold_steps_at_begin=hold_world_steps;
    hold_time_at_begin=*reinterpret_cast<unsigned*>(join_in_progress_rules::game_static+join_in_progress_rules::game_time_at);
}
void __fastcall hold_add_entry(void*,void*,void*) { assert(join_in_progress::area_load);++hold_adds; }
bool hold_logged(std::size_t from,const char* text) {
    for(std::size_t i=from;i<observed_jip_logs.size();++i) if(observed_jip_logs[i].find(text)!=std::string::npos) return true;
    return false;
}
void hold_dump(std::size_t from) { for(std::size_t i=from;i<observed_jip_logs.size();++i) std::cerr<<"  log: "<<observed_jip_logs[i]<<"\n"; }
#define HOLD_EXPECT(from,text) do { if(!hold_logged(from,text)) { std::cerr<<"missing log: "<<text<<"\n";hold_dump(from);assert(false); } } while(0)
// Ticks the production pump poll `count` times, `step` ms apart, with one main-loop iteration each.
unsigned hold_ticks(unsigned count,DWORD step) {
    unsigned ran=0;
    for(unsigned i=0;i<count;++i) { hold_ms+=step;if(hold_iteration()) ++ran;join_in_progress::hold_tick(); }
    return ran;
}
// Main-loop iterations with no pump in between (the frames between a synchronous zone load's end and the next pump).
unsigned hold_iterations(unsigned count) { unsigned ran=0;for(unsigned i=0;i<count;++i) if(hold_iteration()) ++ran;return ran; }
// The production deferred start of a cohort restart (the existing-member path with its natives stubbed).
void hold_deferred_start(bool member) {
    auto& e=join_in_progress::epoch;
    e.active=true;e.requested=true;e.started=false;
    join_in_progress::existing_cohort_member=member;join_in_progress::deferred_start_task=0;join_in_progress::late_restart=false;
    join_in_progress::deferred_campaign_start(nullptr);
    e={};join_in_progress::existing_cohort_member=false;
}
// ---- the overlay on the fake device ----
fake_d3d8::device* hold_device=nullptr;
constexpr std::uint32_t hold_scene=0xff204060u;
// Draws one frame's overlay through the hook the module registered; returns how many back-buffer pixels changed.
unsigned hold_frame() {
    hold_device->back->fill(hold_scene);
    assert(display::fixture_overlay==&join_in_progress::hold_overlay_draw);
    display::fixture_overlay(hold_device);
    unsigned changed=0;
    for(unsigned y=0;y<hold_device->back->desc.height;++y) for(unsigned x=0;x<hold_device->back->desc.width;++x) if(hold_device->back->pixel(x,y)!=hold_scene) ++changed;
    return changed;
}
// The panel the overlay drew for `text`: the border colour on its edge, the scene untouched outside it, white text inside.
void hold_check_panel(const char* text) {
    const auto& bb=*hold_device->back;
    const auto p=hold_overlay::render(text,bb.desc.width,bb.desc.height);
    const auto r=hold_overlay::place(p,bb.desc.width,bb.desc.height);
    assert(p.width>40 && p.height>10 && r.right-r.left==LONG(p.width) && r.left==(LONG(bb.desc.width)-LONG(p.width))/2);
    assert((bb.pixel(r.left,r.top)&0xffffffu)==0x5a8cd8u && (bb.pixel(r.right-1,r.bottom-1)&0xffffffu)==0x5a8cd8u); // border
    assert(bb.pixel(r.left-1,r.top)==hold_scene && bb.pixel(r.right,r.bottom)==hold_scene && bb.pixel(0,0)==hold_scene);  // outside: the scene
    unsigned white=0;
    for(LONG y=r.top;y<r.bottom;++y) for(LONG x=r.left;x<r.right;++x) if((bb.pixel(x,y)&0xffffffu)==0xffffffu) ++white;
    assert(white>50); // the text's fully covered pixels
}
void run_join_hold() {
    using namespace join_in_progress_rules;
    using join_in_progress_core::join_hold;
    using join_in_progress_core::stage;
    auto& machine=join_in_progress::machine;
    redirect(clock_getter,hold_clock_get);hold_clock_vt[0x28/4]=unsigned(reinterpret_cast<std::uintptr_t>(&hold_clock_seconds));
    redirect(scheduler_getter,hold_scheduler_get);
    redirect(clock_reset_step,noop);redirect(network_setup,noop_one);redirect(pause_getter,pause_get);redirect(pause_set,noop_one);redirect(clear_flags,noop_one);
    redirect(zones_getter,zones);redirect(campaign_begin,hold_campaign_begin);
    redirect(session_getter,session);redirect(manager_getter,manager);
    // the native add's natives (run_add): controller slot, seat activation, player announce, the add itself
    redirect(add_entry,hold_add_entry);redirect(area_barrier,barrier);
    redirect(controllers_getter,controllers_get);redirect(seats_getter,seats_get);redirect(players_getter,players_get);
    redirect(record_controller,noop_one);
    controller_vt[controller_assign_slot/4]=unsigned(reinterpret_cast<std::uintptr_t>(assign));
    seat_vt[seats_activate_slot/4]=unsigned(reinterpret_cast<std::uintptr_t>(noop_one));
    seat_vt[seats_update_slot/4]=unsigned(reinterpret_cast<std::uintptr_t>(noop));
    player_vt[player_added_slot/4]=unsigned(reinterpret_cast<std::uintptr_t>(player_add));
    join_in_progress::hold_clock=&hold_fake_ms;
    join_in_progress::hold_zone_loading=&hold_fake_loading;
    join_in_progress::hold.configure(6);
    assert(join_in_progress::hold.cap_ms==6000 && join_in_progress_core::join_hold_default_seconds==10); // the fixture runs JoinHoldSeconds=6 (the default is 10)
    // the overlay hook the production install registers (only with the hold on)
    display::fixture_overlay=&join_in_progress::hold_overlay_draw;
    hold_device=fake_d3d8::make_device(640,360,d3d8::format_x8r8g8b8);
    auto* draws=reinterpret_cast<unsigned*>(rng_count);auto* frame=reinterpret_cast<unsigned short*>(native_frame);
    auto* game_time=reinterpret_cast<unsigned*>(game_static+game_time_at);
    // Hold off: the bridge is retail's own test for every bit pattern (the section runs exactly when bit 1 is set).
    join_in_progress::world_hold=0;join_in_progress::world_hold_skips=0;
    assert(hold_gate_skips(0x00) && !hold_gate_skips(0x02) && hold_gate_skips(0xfd) && !hold_gate_skips(0xff) && join_in_progress::world_hold_skips==0);
    assert(hold_frame()==0); // no hold: the overlay draws nothing

    // ---- 1. Host, one newcomer, through the production admission; nothing simulates from the admission to the restart ----
    put(local,record_flags,1u);put(local,0x18,0);put(remote,0x18,1);put(existing,0x18,2);
    put(s,0x26c,nodes[0]);put(s,0x278,2u);put(nodes[0],node_record,local);put(nodes[0],node_next,nodes[1]);put(nodes[1],node_record,remote);put(nodes[1],node_next,static_cast<void*>(nullptr));
    machine.reset();machine.started=true;
    assert(machine.defer_add(remote,1,false,hold_ms) && machine.pending(remote)); // admitted after Start Game: pending
    *draws=3533;*frame=1890;*game_time=0x427b8a2b;hold_world_steps=0;
    assert(hold_iterations(23)==23 && *draws==3556); // before the zone load the cohort plays (the section runs)
    *draws=3533;*game_time=0x427b8a2b;hold_world_steps=0;
    // the exit: the zone loader reaches the synchronous area-load boundary (0x484def) - the production bridge admits
    auto at=observed_jip_logs.size();
    hold_loading=true;
    join_in_progress::area_barrier_bridge(s,nullptr,5.f);
    HOLD_EXPECT(at,"join in progress: zone load - adding pending player 1");
    HOLD_EXPECT(at,"join hold: armed on this PC (host) at the admission of player 1 (the native add at the area-load boundary, before the zone loads): no world update runs on this PC from here; the hold opens when this zone load ends (frame 1890, synchronized draws 3533");
    assert(hold_adds==1 && machine.seats[0].at==stage::adding && join_in_progress::world_hold==1 && join_in_progress::hold.active() && join_in_progress::hold.armed);
    assert(hold_frame()==0); // inside the zone load: the load screen, no message
    // the synchronous load itself draws (spawns) on every peer alike - not world-section work
    *draws=3698;*frame=1913;
    hold_loading=false; // the load returned; frames run before the next pump (live: 04.890 -> 04.987)
    assert(hold_iterations(3)==0 && *draws==3698 && hold_world_steps==0); // AQ's gap: the host ran here alone; now skipped
    at=observed_jip_logs.size();
    hold_ms+=97;join_in_progress::hold_tick(); // the first pump after the load opens the hold
    HOLD_EXPECT(at,"join hold: the game simulation is held on this PC (host) from the end of the zone load that admitted the newcomer (armed 97 ms earlier at the admission; 3 world update(s) skipped since): 1 newcomer(s), cap 6000 ms ([Online] JoinHoldSeconds=6); frame 1913, synchronized draws 3698, game time 427b8a2b");
    HOLD_EXPECT(at,"(since this PC's zone load ended: frame 1913 -> 1913, synchronized draws 3698 -> 3698 (+0), game time 427b8a2b -> 427b8a2b)");
    HOLD_EXPECT(at,"shows \"A player is joining...\"");
    assert(!join_in_progress::hold.armed && join_in_progress::world_hold==1);
    // the frame after the opening shows the message (the production overlay on the fake device), logged once
    at=observed_jip_logs.size();
    assert(hold_frame()>1000);hold_check_panel("A player is joining...");
    HOLD_EXPECT(at,"join hold: message shown (\"A player is joining...\", blend path, panel ");
    assert(hold_frame()>1000 && !hold_logged(at+1,"message shown")); // every frame draws it; the line is written once
    // the native add's wait (live ZI-1bp: 1.19 s from the load's end to the capture): no world step, no draw, no game time
    put(m,manager_wait,static_cast<unsigned char>(1));
    assert(hold_ticks(36,33)==0 && *draws==3698 && *game_time==0x427b8a2b && hold_world_steps==0);
    // the wait ends: the image capture (start_image -> hold_capture) sees the world exactly as the load left it
    put(m,manager_wait,static_cast<unsigned char>(0));
    machine.seats[0].at=stage::streaming;at=observed_jip_logs.size();
    join_in_progress::hold_capture();
    HOLD_EXPECT(at,"join hold: image captured while held, 1188 ms after the opening (39 world update(s) skipped since the admission); frame 1913 -> 1913, synchronized draws 3698 -> 3698, game time 427b8a2b -> 427b8a2b - no simulation step between the zone load and the capture");
    // streaming ~3.1 s, then the newcomer's J (the seat goes to starting): ready, still held
    assert(hold_ticks(94,33)==0);
    machine.seats[0].at=stage::starting;at=observed_jip_logs.size();
    assert(hold_ticks(1,33)==0);
    HOLD_EXPECT(at,"join hold: every newcomer is ready after 4323 ms (1 newcomer(s) have the campaign image); held until this PC's restart begins");
    assert(hold_ticks(30,33)==0 && join_in_progress::world_hold==1 && *draws==3698); // the native start's 1 s wait
    // the production deferred start: the hold ends first, the native campaign start sees the world exactly as loaded
    at=observed_jip_logs.size();
    hold_deferred_start(true);
    assert(hold_begins==1 && !hold_at_begin && hold_draws_at_begin==3698 && hold_steps_at_begin==0 && hold_time_at_begin==0x427b8a2b);
    HOLD_EXPECT(at,"join hold: 5313 ms, 1 newcomer(s) ready - this PC's cohort restart begins (164 world update(s) skipped while held; frame 1913 -> 1913, synchronized draws 3698 -> 3698, game time 427b8a2b -> 427b8a2b)");
    HOLD_EXPECT(at,"join hold: message removed");
    assert(!join_in_progress::hold.active() && join_in_progress::world_hold==0);
    assert(hold_frame()==0); // the restart's frames: no message
    assert(hold_iteration() && *draws==3699); // the restarted world runs
    machine.reset();join_in_progress::area_load=false;join_in_progress::epoch={};
    std::cout<<"PASS join hold (host, 1 newcomer, production admission): armed at the area-load boundary's native add, opened at the first pump after the zone load (3 frames between the load's end and that pump skipped - the window the host ran alone in live-060zi), 164 iterations to the restart with no world step, no draw (3698 -> 3698) and no game time; the capture logged no step; the production overlay drew \"A player is joining...\" on every held frame (blend path, logged once) and nothing after the restart\n";

    // ---- 1b. A capture before any pump after the zone load opens the armed hold itself (start_image needs the zone loaded) ----
    machine.reset();machine.started=true;assert(machine.defer_add(remote,1,false,hold_ms));
    *draws=3000;hold_loading=true;join_in_progress::area_barrier_bridge(s,nullptr,5.f);hold_loading=false;
    assert(hold_iterations(2)==0 && join_in_progress::hold.armed);
    machine.seats[0].at=stage::streaming;at=observed_jip_logs.size();
    join_in_progress::hold_capture();
    HOLD_EXPECT(at,"from the end of the zone load that admitted the newcomer (armed 0 ms earlier at the admission; 2 world update(s) skipped since)");
    HOLD_EXPECT(at,"join hold: image captured while held, 0 ms after the opening (2 world update(s) skipped since the admission); frame 1913 -> 1913, synchronized draws 3000 -> 3000");
    HOLD_EXPECT(at,"- no simulation step between the zone load and the capture");
    assert(!join_in_progress::hold.armed && hold_frame()>1000);
    join_in_progress::hold_end(join_hold::event::session);assert(!join_in_progress::world_hold && hold_frame()==0);
    machine.reset();join_in_progress::area_load=false;join_in_progress::epoch={};
    std::cout<<"PASS join hold (host, capture before the first pump): the capture opens the armed hold first and logs no step since the zone load\n";

    // ---- 2. Host, two newcomers admitted at the same zone load (both run the native add; the second rides the image) ----
    put(nodes[1],node_next,nodes[2]);put(nodes[2],node_record,existing);put(nodes[2],node_next,static_cast<void*>(nullptr));put(s,0x278,3u);
    machine.reset();machine.started=true;
    assert(machine.defer_add(remote,1,false,hold_ms) && machine.defer_add(existing,2,false,hold_ms));
    *draws=4000;hold_world_steps=0;hold_adds=0;at=observed_jip_logs.size();
    hold_loading=true;
    redirect(add_entry,hold_add_entry);
    join_in_progress::area_barrier_bridge(s,nullptr,5.f);
    assert(hold_adds==2 && join_in_progress::hold.armed && join_in_progress::hold.newcomers==2);
    HOLD_EXPECT(at,"join hold: player 2 joins the hold (2 newcomer(s))");
    hold_loading=false;assert(hold_iterations(2)==0);hold_ms+=50;join_in_progress::hold_tick();
    HOLD_EXPECT(at,"from the end of the zone load that admitted the newcomer");
    assert(hold_frame()>1000);hold_check_panel("2 players are joining...");
    machine.seats[0].at=stage::streaming;machine.seats[1].at=stage::streaming; // the second rides the transfer (start_image's rider path adds nothing)
    assert(hold_ticks(20,100)==0 && join_in_progress::hold.newcomers==2);
    machine.seats[0].at=stage::starting;assert(hold_ticks(5,100)==0 && !join_in_progress::hold.all_ready); // one of two: still waiting
    machine.seats[1].at=stage::starting;at=observed_jip_logs.size();
    assert(hold_ticks(1,100)==0);HOLD_EXPECT(at,"every newcomer is ready after 2600 ms (2 newcomer(s)");
    at=observed_jip_logs.size();hold_deferred_start(true);
    assert(hold_begins==2 && !hold_at_begin && hold_draws_at_begin==4000 && hold_steps_at_begin==0);
    HOLD_EXPECT(at,"join hold: 2600 ms, 2 newcomer(s) ready - this PC's cohort restart begins (28 world update(s) skipped");
    put(nodes[1],node_next,static_cast<void*>(nullptr));put(s,0x278,2u);
    machine.reset();join_in_progress::area_load=false;join_in_progress::epoch={};
    std::cout<<"PASS join hold (host, 2 newcomers): both native adds at the one zone load arm one hold (\"2 players are joining...\" drawn), ready only when both images are in, released at the restart with no world step\n";

    // ---- 3. The cap counts from the zone load's end, not the admission; a slow joiner then ends the hold ----
    machine.reset();machine.started=true;assert(machine.defer_add(remote,1,false,hold_ms));
    *draws=100;hold_world_steps=0;
    hold_loading=true;join_in_progress::area_barrier_bridge(s,nullptr,5.f);
    assert(hold_ticks(70,100)==0 && join_in_progress::hold.armed && join_in_progress::world_hold==1); // a 7 s zone load: no cap while armed
    hold_loading=false;assert(hold_ticks(1,100)==0 && !join_in_progress::hold.armed);
    machine.seats[0].at=stage::streaming;
    assert(hold_ticks(59,100)==0 && join_in_progress::world_hold==1); // 5900 ms after the opening
    at=observed_jip_logs.size();
    assert(hold_ticks(1,100)==0); // the 6000 ms poll ends it after this iteration's gate
    HOLD_EXPECT(at,"join hold: cap reached after 6000 ms (0 of 1 newcomer(s) ready) - this PC simulates again until the restart, as without the hold; the newcomer is not refused (131 world update(s) skipped while held");
    assert(!join_in_progress::hold.active() && join_in_progress::world_hold==0 && hold_frame()==0);
    assert(hold_ticks(10,100)==10 && *draws==110); // today's path: the host plays on while the image goes out
    assert(machine.seats[0].at==stage::streaming && machine.seats[0].record==remote); // the seat is untouched
    machine.seats[0].at=stage::starting;at=observed_jip_logs.size();hold_deferred_start(true);
    assert(hold_begins==3 && !hold_logged(at,"join hold:")); // the restart still runs; nothing more to release
    // a zone load that never ends: the armed limit (30 s) releases the host
    machine.reset();machine.started=true;assert(machine.defer_add(remote,1,false,hold_ms));
    hold_loading=true;join_in_progress::area_barrier_bridge(s,nullptr,5.f);at=observed_jip_logs.size();
    assert(hold_ticks(300,100)==0);
    HOLD_EXPECT(at,"join hold: ended after 30000 ms - the zone load that admitted the newcomer did not end within 30000 ms; this PC simulates again");
    assert(!join_in_progress::world_hold && hold_ticks(1,100)==1);
    hold_loading=false;machine.reset();join_in_progress::area_load=false;join_in_progress::epoch={};
    std::cout<<"PASS join hold (cap): a 7 s zone load does not use the cap; the cap (6000 ms) counts from the zone load's end, ends the hold (logged), the host simulates again, the seat and the transfer are untouched and the restart still runs; a zone load that never ends is released after 30 s\n";

    // ---- 4. The joiner is killed / leaves: armed (its add given up) or open (streaming): the hold ends, no stall ----
    machine.reset();machine.started=true;assert(machine.defer_add(remote,1,false,hold_ms));
    hold_loading=true;join_in_progress::area_barrier_bridge(s,nullptr,5.f);hold_loading=false;
    assert(hold_ticks(15,100)==0);
    machine.seats[0]={}; // advance(): "player 1 left while streaming; the seat is free again"
    at=observed_jip_logs.size();assert(hold_ticks(1,100)==0);
    HOLD_EXPECT(at,"join hold: ended after 1500 ms - every newcomer of this hold left (seat freed); the session plays on");
    assert(join_in_progress::world_hold==0 && hold_ticks(3,100)==3);
    machine.reset();machine.started=true;assert(machine.defer_add(remote,1,false,hold_ms));
    hold_loading=true;join_in_progress::area_barrier_bridge(s,nullptr,5.f);
    machine.seats[0]={}; // left during the zone load: the first pump after it ends the armed hold
    hold_loading=false;at=observed_jip_logs.size();assert(hold_ticks(1,100)==0);
    HOLD_EXPECT(at,"every newcomer of this hold left");
    assert(join_in_progress::world_hold==0);
    machine.reset();join_in_progress::area_load=false;join_in_progress::epoch={};
    std::cout<<"PASS join hold (joiner gone): a freed seat ends the hold at the next pump (open, or armed through the zone load) and the host plays on\n";

    // ---- 5. The host quits during the hold: the session's end releases it (the production pump path) ----
    machine.reset();machine.started=true;assert(machine.defer_add(remote,1,false,hold_ms));
    hold_loading=true;join_in_progress::area_barrier_bridge(s,nullptr,5.f);hold_loading=false;
    assert(hold_ticks(10,100)==0 && hold_frame()>1000);
    put(s,0x26c,static_cast<void*>(nullptr));at=observed_jip_logs.size();
    assert(!join_in_progress::guarded_advance(false,nullptr));
    HOLD_EXPECT(at,"join hold: ended after 900 ms by the session's end");
    assert(!join_in_progress::world_hold && !machine.started && !machine.any() && hold_frame()==0);
    put(s,0x26c,nodes[0]);join_in_progress::area_load=false;join_in_progress::epoch={};
    std::cout<<"PASS join hold (host Quit): the session's end in the production pump path releases the hold and the message\n";

    // ---- 6. Existing participants (3 and 4 peers): the production campaign_notice of the late-restart 0x47 ----
    alignas(4) unsigned char body[8]{2,0,0,0,0,0,0,0};
    alignas(4) unsigned char event[8]{};event[0]=opcode_prepare;put(event,4,static_cast<void*>(body));
    for(int joining=1;joining<=3;++joining) {
        machine.reset();machine.started=true;join_in_progress::mesh_newcomers=joining; // "new mesh participant" per newcomer
        const auto marker=join_in_progress_core::prepare_stamp(true);std::memcpy(body+join_in_progress_core::prepare_marker_at,&marker,2);
        // this PC's own zone load ends (the pump sees the flag fall), then the 0x47 arrives with nothing simulated between
        hold_loading=true;join_in_progress::area_barrier_bridge(s,nullptr,5.f);*draws=3698;*frame=1913;hold_loading=false;join_in_progress::hold_tick();
        hold_world_steps=0;at=observed_jip_logs.size();
        join_in_progress::campaign_notice(event);
        assert(join_in_progress::late_restart && join_in_progress::existing_cohort_member && join_in_progress::hold.active() && join_in_progress::hold.who==join_hold::role::member);
        assert(join_in_progress::hold.newcomers==joining && !join_in_progress::hold.armed);
        HOLD_EXPECT(at,"join hold: the game simulation is held on this PC (existing participant) from the late-restart 0x47");
        HOLD_EXPECT(at,"since this PC's zone load ended: frame 1913 -> 1913, synchronized draws 3698 -> 3698 (+0)");
        assert(hold_frame()>1000);hold_check_panel(join_in_progress_core::join_hold_text(joining));
        assert(hold_ticks(40,100)==0 && !join_in_progress::hold.all_ready); // a member cannot see seats: only its restart or the cap
        at=observed_jip_logs.size();hold_deferred_start(true);
        char line[96];std::snprintf(line,sizeof line,"join hold: 4000 ms, %d newcomer(s) ready - this PC's cohort restart begins",joining);
        HOLD_EXPECT(at,line);
        assert(hold_draws_at_begin==3698 && hold_steps_at_begin==0 && join_in_progress::mesh_newcomers==0 && hold_frame()==0);
        join_in_progress::late_restart=false;
    }
    // a member whose host died: the cap releases it (then the host-left path runs as before, off the network)
    machine.started=true;join_in_progress::mesh_newcomers=1;join_in_progress::campaign_notice(event);
    at=observed_jip_logs.size();assert(hold_ticks(60,100)==0);HOLD_EXPECT(at,"join hold: cap reached after 6000 ms (0 of 1 newcomer(s) ready)");
    assert(!join_in_progress::world_hold);
    join_in_progress::epoch={};join_in_progress::late_restart=false;
    // the newcomer itself (Start Game never passed in this session) and a lobby START (no marker) never hold
    machine.started=false;join_in_progress::campaign_notice(event);assert(!join_in_progress::hold.active() && !join_in_progress::world_hold);
    join_in_progress::epoch={};join_in_progress::late_restart=false;
    machine.started=true;std::memset(body+join_in_progress_core::prepare_marker_at,0,2);join_in_progress::campaign_notice(event);
    assert(!join_in_progress::late_restart && !join_in_progress::hold.active() && !join_in_progress::world_hold && hold_frame()==0);
    join_in_progress::epoch={};machine.reset();join_in_progress::existing_cohort_member=false;
    std::cout<<"PASS join hold (existing participants, 1-3 newcomers = 3 and 4 peers): the production late-restart 0x47 opens the hold with the count from the mesh and logs +0 draws since this PC's zone load; the message is drawn; only the restart or the cap ends it; the newcomer and a lobby START never hold\n";

    // ---- 7. The overlay's other paths: 16-bit back buffer (blend), multisampled (clear, viewport restored), refusals ----
    hold_overlay::release();
    {
        join_in_progress::hold_message="A player is joining...";join_in_progress::hold_message_logged=false;join_in_progress::hold_message_failed=false;
        auto* d16=fake_d3d8::make_device(320,240,d3d8::format_r5g6b5);
        d16->back->fill(0x2104);display::fixture_overlay(d16);
        const auto p=hold_overlay::render("A player is joining...",320,240);const auto r=hold_overlay::place(p,320,240);
        assert(d16->back->pixel(r.left,r.top)==hold_overlay::to16(0xff5a8cd8u,d3d8::format_r5g6b5) && d16->back->pixel(0,0)==0x2104 && d16->copies==2);
        hold_overlay::release();
        auto* ms=fake_d3d8::make_device(640,360,d3d8::format_x8r8g8b8,2);
        ms->back->fill(hold_scene);const auto before=ms->view;at=observed_jip_logs.size();join_in_progress::hold_message_logged=false;
        display::fixture_overlay(ms);
        HOLD_EXPECT(at,"join hold: message shown (\"A player is joining...\", clear path");
        const auto p2=hold_overlay::render("A player is joining...",640,360);const auto r2=hold_overlay::place(p2,640,360);
        assert(ms->copies==0 && ms->clears>=3 && ms->back->pixel(r2.left,r2.top)==0xff5a8cd8u && ms->back->pixel(r2.left+int(p2.border)+1,r2.top+int(p2.border)+1)==0xff101830u);
        assert(ms->back->pixel(0,0)==hold_scene && std::memcmp(&ms->view,&before,sizeof before)==0); // the engine's viewport is back
        ms->render_target_is_back=false;at=observed_jip_logs.size();join_in_progress::hold_message_failed=false;
        ms->back->fill(hold_scene);display::fixture_overlay(ms);display::fixture_overlay(ms);
        HOLD_EXPECT(at,"join hold: the on-screen message could not be drawn (the render target is not the back buffer");
        assert(ms->back->pixel(r2.left,r2.top)==hold_scene);
        unsigned failures=0;for(std::size_t i=at;i<observed_jip_logs.size();++i) failures+=observed_jip_logs[i].find("could not be drawn")!=std::string::npos;
        assert(failures==1); // once per hold
        // a refused CopyRects falls back to the clear path
        hold_overlay::release();auto* refuse=fake_d3d8::make_device(640,360,d3d8::format_x8r8g8b8);refuse->fail_copy=d3d8::err_invalid_call;
        refuse->back->fill(hold_scene);const auto o=hold_overlay::draw(refuse,"A player is joining...");
        assert(o.drawn==hold_overlay::path::clear && refuse->back->pixel(r2.left,r2.top)==0xff5a8cd8u);
        // pure parts: the blend and the 16-bit round trips
        assert(hold_overlay::blend(0xff000000u,0xffffffffu)==0xffffffffu && hold_overlay::blend(0xff123456u,0x00ffffffu)==0xff123456u);
        assert(hold_overlay::blend(0xff000000u,0x80ffffffu)==0xff808080u);
        for(const DWORD f:{d3d8::format_r5g6b5,d3d8::format_x1r5g5b5,d3d8::format_a1r5g5b5})
            for(const std::uint32_t c:{0xff000000u,0xffffffffu,0xff5a8cd8u,0xff101830u}) assert(hold_overlay::to16(hold_overlay::from16(hold_overlay::to16(c,f),f),f)==hold_overlay::to16(c,f));
        join_in_progress::hold_message=nullptr;hold_overlay::release();
        fake_d3d8::current=hold_device;
    }
    std::cout<<"PASS join hold (overlay paths): a 16-bit back buffer blends, a multisampled one takes the clear path with the engine's viewport restored, a render target that is not the back buffer draws nothing (logged once), a refused CopyRects falls back to the clear path\n";

    // ---- 7b. The built-in default (10 s, Owen 11-01): a slow joiner is still held at 9.9 s and released at 10.0 s ----
    join_in_progress::hold.configure(join_in_progress_core::join_hold_default_seconds);
    assert(join_in_progress::hold.cap_ms==10000);
    machine.reset();machine.started=true;assert(machine.defer_add(remote,1,false,hold_ms));
    hold_loading=true;join_in_progress::area_barrier_bridge(s,nullptr,5.f);hold_loading=false;
    assert(hold_ticks(1,100)==0 && !join_in_progress::hold.armed); // opened at this pump
    machine.seats[0].at=stage::streaming;
    assert(hold_ticks(99,100)==0 && join_in_progress::world_hold==1); // 9900 ms after the opening
    at=observed_jip_logs.size();assert(hold_ticks(1,100)==0);
    HOLD_EXPECT(at,"join hold: cap reached after 10000 ms (0 of 1 newcomer(s) ready)");
    assert(!join_in_progress::world_hold && hold_ticks(1,100)==1);
    machine.reset();join_in_progress::area_load=false;join_in_progress::epoch={};
    join_in_progress::hold.configure(6);
    std::cout<<"PASS join hold (default 10 s): held 9900 ms after the zone load's end, the cap releases it at 10000 ms\n";

    // ---- 8. [Online] JoinHoldSeconds=0: off (today's path) ----
    join_in_progress::hold.configure(0);
    machine.reset();machine.started=true;assert(machine.defer_add(remote,1,false,hold_ms));
    *draws=500;hold_loading=true;join_in_progress::area_barrier_bridge(s,nullptr,5.f);hold_loading=false;
    assert(!join_in_progress::hold.active() && !join_in_progress::world_hold && hold_ticks(5,100)==5 && *draws==505 && hold_frame()==0);
    join_in_progress::hold.configure(99);assert(join_in_progress::hold.cap_ms==15000);
    join_in_progress::hold.configure(6);machine.reset();join_in_progress::area_load=false;join_in_progress::epoch={};
    std::cout<<"PASS join hold (JoinHoldSeconds=0): the admission arms nothing, nothing is held or drawn; the range is clamped to 0..15\n";
    join_in_progress::hold_clock=&GetTickCount;
    join_in_progress::hold_zone_loading=&join_in_progress::zone_load_pending;
}
